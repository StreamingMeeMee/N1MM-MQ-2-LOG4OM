#include "mq_consumer.h"

#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h> /* struct timeval */
#else
#include <sys/time.h> /* struct timeval */
#endif

#define AMQP_APP_CHANNEL 1

static int check_amqp_reply(amqp_rpc_reply_t reply, const char *context, char *errbuf, size_t errbuf_len) {
    switch (reply.reply_type) {
        case AMQP_RESPONSE_NORMAL:
            return 0;
        case AMQP_RESPONSE_NONE:
            snprintf(errbuf, errbuf_len, "%s: missing RPC reply type", context);
            return -1;
        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
            snprintf(errbuf, errbuf_len, "%s: %s", context, amqp_error_string2(reply.library_error));
            return -1;
        case AMQP_RESPONSE_SERVER_EXCEPTION:
            if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
                amqp_connection_close_t *m = (amqp_connection_close_t *)reply.reply.decoded;
                snprintf(errbuf, errbuf_len, "%s: server connection error %u: %.*s",
                         context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
            } else if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
                amqp_channel_close_t *m = (amqp_channel_close_t *)reply.reply.decoded;
                snprintf(errbuf, errbuf_len, "%s: server channel error %u: %.*s",
                         context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
            } else {
                snprintf(errbuf, errbuf_len, "%s: unknown server error, method id 0x%08X", context, reply.reply.id);
            }
            return -1;
        default:
            snprintf(errbuf, errbuf_len, "%s: unknown reply type", context);
            return -1;
    }
}

void mq_consumer_init(mq_consumer_t *c, const rabbitmq_config_t *cfg) {
    memset(c, 0, sizeof(*c));
    snprintf(c->host, sizeof(c->host), "%s", cfg->host);
    c->port = cfg->port;
    snprintf(c->username, sizeof(c->username), "%s", cfg->username);
    snprintf(c->password, sizeof(c->password), "%s", cfg->password);
    snprintf(c->vhost, sizeof(c->vhost), "%s", cfg->vhost);
    snprintf(c->queue, sizeof(c->queue), "%s", cfg->contactinfo_queue);
    c->connected = 0;
}

int mq_consumer_connect(mq_consumer_t *c, char *errbuf, size_t errbuf_len) {
    c->conn = amqp_new_connection();
    if (!c->conn) {
        snprintf(errbuf, errbuf_len, "amqp_new_connection failed");
        return -1;
    }

    amqp_socket_t *sock = amqp_tcp_socket_new(c->conn);
    if (!sock) {
        snprintf(errbuf, errbuf_len, "amqp_tcp_socket_new failed");
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    int status = amqp_socket_open(sock, c->host, c->port);
    if (status) {
        snprintf(errbuf, errbuf_len, "cannot connect to %s:%d: %s", c->host, c->port, amqp_error_string2(status));
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    amqp_rpc_reply_t reply = amqp_login(c->conn, c->vhost, 0, 131072, 0,
                                         AMQP_SASL_METHOD_PLAIN, c->username, c->password);
    if (check_amqp_reply(reply, "amqp_login", errbuf, errbuf_len) != 0) {
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    amqp_channel_open(c->conn, AMQP_APP_CHANNEL);
    reply = amqp_get_rpc_reply(c->conn);
    if (check_amqp_reply(reply, "amqp_channel_open", errbuf, errbuf_len) != 0) {
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    amqp_queue_declare(c->conn, AMQP_APP_CHANNEL, amqp_cstring_bytes(c->queue),
                        0 /* passive */, 1 /* durable */, 0 /* exclusive */, 0 /* auto_delete */,
                        amqp_empty_table);
    reply = amqp_get_rpc_reply(c->conn);
    if (check_amqp_reply(reply, "amqp_queue_declare", errbuf, errbuf_len) != 0) {
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    amqp_basic_consume(c->conn, AMQP_APP_CHANNEL, amqp_cstring_bytes(c->queue),
                        amqp_empty_bytes /* consumer tag: server-assigned */,
                        0 /* no_local */, 0 /* no_ack: we ack manually */, 0 /* exclusive */,
                        amqp_empty_table);
    reply = amqp_get_rpc_reply(c->conn);
    if (check_amqp_reply(reply, "amqp_basic_consume", errbuf, errbuf_len) != 0) {
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(c->conn);
        c->conn = NULL;
        return -1;
    }

    c->connected = 1;
    return 0;
}

void mq_consumer_disconnect(mq_consumer_t *c) {
    if (!c->conn) return;
    if (c->connected) {
        amqp_channel_close(c->conn, AMQP_APP_CHANNEL, AMQP_REPLY_SUCCESS);
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
    }
    amqp_destroy_connection(c->conn);
    c->conn = NULL;
    c->connected = 0;
}

int mq_consumer_receive(mq_consumer_t *c, amqp_envelope_t *out_envelope, int timeout_ms,
                         char *errbuf, size_t errbuf_len) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    amqp_maybe_release_buffers(c->conn);

    amqp_rpc_reply_t reply = amqp_consume_message(c->conn, out_envelope, &tv, 0);
    if (reply.reply_type == AMQP_RESPONSE_NORMAL) {
        return 1;
    }
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && reply.library_error == AMQP_STATUS_TIMEOUT) {
        return 0;
    }
    check_amqp_reply(reply, "amqp_consume_message", errbuf, errbuf_len);
    return -1;
}

int mq_consumer_ack(mq_consumer_t *c, uint64_t delivery_tag) {
    return amqp_basic_ack(c->conn, AMQP_APP_CHANNEL, delivery_tag, 0 /* multiple */) == 0 ? 0 : -1;
}

int mq_consumer_nack(mq_consumer_t *c, uint64_t delivery_tag, int requeue) {
    return amqp_basic_reject(c->conn, AMQP_APP_CHANNEL, delivery_tag, requeue ? 1 : 0) == 0 ? 0 : -1;
}
