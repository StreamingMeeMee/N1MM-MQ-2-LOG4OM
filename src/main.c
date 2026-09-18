#include "config.h"
#include "db_client.h"
#include "log.h"
#include "mq_consumer.h"
#include "xmlflat.h"

#include <amqp.h>
#include <mysql.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_UPSERT_FIELDS 256

/* Everything on the configured queue is assumed to be N1MM contactinfo,
 * regardless of the XML root element's name. */
static const char MSG_TYPE[] = "contactinfo";

static volatile int g_shutdown = 0;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_shutdown = 1;
            return TRUE;
        default:
            return FALSE;
    }
}
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <signal.h>
static void sigint_handler(int signum) {
    (void)signum;
    g_shutdown = 1;
}
static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-v] [-c config.json] [-h]\n"
        "  -v            verbose mode: print a line to stdout for each received N1MM message (default: off)\n"
        "  -c <file>     path to JSON config file (default: config.json)\n"
        "  -h            show this help and exit\n",
        prog);
}

/* Schedules the next reconnect attempt using capped exponential backoff,
 * returning the new "attempt no earlier than" time. */
static time_t backoff_next(unsigned *backoff_ms) {
    time_t next = time(NULL) + (time_t)(*backoff_ms / 1000 > 0 ? *backoff_ms / 1000 : 1);
    static const unsigned cap_ms = 30000;
    *backoff_ms = *backoff_ms < cap_ms ? *backoff_ms * 2 : cap_ms;
    return next;
}

/* Parses, maps, and upserts (or acks/nacks as appropriate) one delivered
 * message. Always leaves envelope ack'd/nack'd and doc memory freed;
 * never leaves a message un-acknowledged. */
static void process_message(mq_consumer_t *rmq, db_client_t *db, const app_config_t *cfg,
                             amqp_envelope_t *envelope, time_t *next_db_attempt) {
    const char *body = (const char *)envelope->message.body.bytes;
    size_t body_len = envelope->message.body.len;
    uint64_t tag = envelope->delivery_tag;

    xmlflat_doc_t doc;
    if (xmlflat_parse(body, body_len, &doc) != 0) {
        log_message(MSG_TYPE, "discarded (malformed XML)");
        log_payload(body, body_len);
        mq_consumer_nack(rmq, tag, 0);
        xmlflat_free(&doc);
        return;
    }

    if (!db->connected) {
        log_message(MSG_TYPE,"requeued (MySQL unavailable)");
        mq_consumer_nack(rmq, tag, 1);
        xmlflat_free(&doc);
        return;
    }

    db_field_t fields[MAX_UPSERT_FIELDS];
    char *owned[MAX_UPSERT_FIELDS];
    size_t field_count = 0, owned_count = 0;

    for (size_t i = 0; i < doc.field_count; i++) {
        const char *xml_name = doc.fields[i].name;
        const char *xml_value = doc.fields[i].value;

        const char *column = config_resolve_field(cfg, xml_name);
        if (!column) continue; /* "null"-mapped: drop this field */

        long max_len = -1;
        if (!db_client_has_column(db, column, &max_len)) continue; /* no such column: skip */

        const char *value = xml_value;
        char *owned_buf = NULL;

        /* N1MM sends txfreq/rxfreq in tens-of-Hz; convert to kHz. Fixed
         * unit fix tied to the source field identity, not configurable. */
        if (strcmp(xml_name, "txfreq") == 0 || strcmp(xml_name, "rxfreq") == 0) {
            double tens_of_hz = atof(xml_value);
            owned_buf = (char *)malloc(64);
            snprintf(owned_buf, 64, "%.3f", tens_of_hz / 100.0);
            value = owned_buf;
        }

        if (max_len >= 0 && (long)strlen(value) > max_len) {
            char *truncated = (char *)malloc((size_t)max_len + 1);
            memcpy(truncated, value, (size_t)max_len);
            truncated[max_len] = '\0';
            free(owned_buf);
            owned_buf = truncated;
            value = owned_buf;
        }

        if (field_count < MAX_UPSERT_FIELDS) {
            fields[field_count].column = column;
            fields[field_count].value = value;
            field_count++;
            if (owned_buf) owned[owned_count++] = owned_buf;
        } else {
            free(owned_buf);
        }
    }

    if (field_count == 0) {
        log_message(MSG_TYPE,"discarded (no mappable fields)");
        mq_consumer_nack(rmq, tag, 0);
        for (size_t i = 0; i < owned_count; i++) free(owned[i]);
        xmlflat_free(&doc);
        return;
    }

    int is_conn_err = 0;
    char errbuf[256];
    if (db_client_upsert(db, fields, field_count, &is_conn_err, errbuf, sizeof(errbuf)) == 0) {
        log_message(MSG_TYPE,"upserted");
        mq_consumer_ack(rmq, tag);
    } else if (is_conn_err) {
        fprintf(stderr, "MySQL error: %s\n", errbuf);
        log_message(MSG_TYPE,"requeued (MySQL error)");
        db_client_disconnect(db);
        *next_db_attempt = 0;
        mq_consumer_nack(rmq, tag, 1);
    } else {
        fprintf(stderr, "MySQL error: %s\n", errbuf);
        log_message(MSG_TYPE,"discarded (query error)");
        mq_consumer_nack(rmq, tag, 0);
    }

    for (size_t i = 0; i < owned_count; i++) free(owned[i]);
    xmlflat_free(&doc);
}

int main(int argc, char **argv) {
    int verbose = 0;
    const char *config_path = "config.json";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    log_init(verbose);

    app_config_t cfg;
    char errbuf[512];
    if (config_load(config_path, &cfg, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Failed to load config '%s': %s\n", config_path, errbuf);
        return 1;
    }

    if (mysql_library_init(0, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to initialize MySQL client library\n");
        return 1;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
#endif

    mq_consumer_t rmq;
    mq_consumer_init(&rmq, &cfg.rabbitmq);
    db_client_t db;
    db_client_init(&db, &cfg.mysql);

    unsigned rmq_backoff_ms = 1000, db_backoff_ms = 1000;
    time_t next_rmq_attempt = 0, next_db_attempt = 0;
    long processed_count = 0;

    if (cfg.process_limit > 0) {
        fprintf(stderr, "N1MM-MQ-2-LOG4OM starting. Will process %ld message(s) then exit. Press Ctrl+C to stop early.\n",
                cfg.process_limit);
    } else {
        fprintf(stderr, "N1MM-MQ-2-LOG4OM starting. Press Ctrl+C to stop.\n");
    }

    while (!g_shutdown) {
        if (!rmq.connected && time(NULL) >= next_rmq_attempt) {
            if (mq_consumer_connect(&rmq, errbuf, sizeof(errbuf)) == 0) {
                rmq_backoff_ms = 1000;
                fprintf(stderr, "connected to RabbitMQ at %s:%d, consuming queue '%s'\n",
                        rmq.host, rmq.port, rmq.queue);
            } else {
                unsigned wait_ms = rmq_backoff_ms;
                next_rmq_attempt = backoff_next(&rmq_backoff_ms);
                fprintf(stderr, "RabbitMQ connection failed: %s (retrying in %ums)\n", errbuf, wait_ms);
            }
        }

        if (!db.connected && time(NULL) >= next_db_attempt) {
            if (db_client_connect(&db, errbuf, sizeof(errbuf)) == 0) {
                db_backoff_ms = 1000;
                fprintf(stderr, "connected to MySQL %s:%d, table '%s.%s' (%zu columns discovered)\n",
                        db.host, db.port, db.database, db.table, db.column_count);
            } else {
                unsigned wait_ms = db_backoff_ms;
                next_db_attempt = backoff_next(&db_backoff_ms);
                fprintf(stderr, "MySQL connection failed: %s (retrying in %ums)\n", errbuf, wait_ms);
            }
        }

        if (!rmq.connected) {
            sleep_ms(500);
            continue;
        }

        amqp_envelope_t envelope;
        int rc = mq_consumer_receive(&rmq, &envelope, 1000, errbuf, sizeof(errbuf));
        if (rc == 0) continue; /* timeout: loop to re-check shutdown/reconnect */
        if (rc < 0) {
            fprintf(stderr, "RabbitMQ error: %s\n", errbuf);
            mq_consumer_disconnect(&rmq);
            next_rmq_attempt = 0;
            continue;
        }

        process_message(&rmq, &db, &cfg, &envelope, &next_db_attempt);
        amqp_destroy_envelope(&envelope);

        processed_count++;
        if (cfg.process_limit > 0 && processed_count >= cfg.process_limit) {
            fprintf(stderr, "Reached process_limit (%ld message(s)); shutting down.\n", cfg.process_limit);
            g_shutdown = 1;
        }
    }

    fprintf(stderr, "Shutting down...\n");
    mq_consumer_disconnect(&rmq);
    db_client_disconnect(&db);
    mysql_library_end();
    return 0;
}
