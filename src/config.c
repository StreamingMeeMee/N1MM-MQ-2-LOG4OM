#include "config.h"
#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Case-insensitive string equality (no dependency on POSIX strcasecmp /
 * Windows _stricmp, so this stays portable across compilers). */
static int str_ci_equals(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static char *read_file(const char *path, char *errbuf, size_t errbuf_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(errbuf, errbuf_size, "cannot open file: %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(errbuf, errbuf_size, "cannot seek file: %s", path);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        snprintf(errbuf, errbuf_size, "cannot determine file size: %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        snprintf(errbuf, errbuf_size, "out of memory reading file");
        return NULL;
    }
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_bytes] = '\0';
    return buf;
}

int config_load(const char *path, app_config_t *out_cfg, char *errbuf, size_t errbuf_size) {
    memset(out_cfg, 0, sizeof(*out_cfg));

    char *text = read_file(path, errbuf, errbuf_size);
    if (!text) return -1;

    char jerr[256];
    json_value_t *root = json_parse(text, jerr, sizeof(jerr));
    free(text);
    if (!root) {
        snprintf(errbuf, errbuf_size, "JSON parse error: %s", jerr);
        return -1;
    }
    if (root->type != JSON_OBJECT) {
        snprintf(errbuf, errbuf_size, "config root must be a JSON object");
        json_free(root);
        return -1;
    }

    /* ---- rabbitmq ---- */
    const json_value_t *rmq = json_object_get(root, "rabbitmq");
    if (!rmq || rmq->type != JSON_OBJECT) {
        snprintf(errbuf, errbuf_size, "missing 'rabbitmq' object");
        json_free(root);
        return -1;
    }
    const char *rmq_host = json_get_string(rmq, "host", NULL);
    const char *rmq_user = json_get_string(rmq, "username", NULL);
    const char *rmq_pass = json_get_string(rmq, "password", NULL);
    const char *rmq_queue = json_get_string(rmq, "contactinfo_queue", NULL);
    const char *rmq_vhost = json_get_string(rmq, "vhost", "/");
    double rmq_port = json_get_number(rmq, "port", 5672);

    if (!rmq_host || !rmq_user || !rmq_pass || !rmq_queue) {
        snprintf(errbuf, errbuf_size, "'rabbitmq' requires 'host', 'username', 'password', and 'contactinfo_queue'");
        json_free(root);
        return -1;
    }
    if (rmq_port <= 0 || rmq_port > 65535) {
        snprintf(errbuf, errbuf_size, "'rabbitmq.port' must be between 1 and 65535");
        json_free(root);
        return -1;
    }

    snprintf(out_cfg->rabbitmq.host, sizeof(out_cfg->rabbitmq.host), "%s", rmq_host);
    snprintf(out_cfg->rabbitmq.username, sizeof(out_cfg->rabbitmq.username), "%s", rmq_user);
    snprintf(out_cfg->rabbitmq.password, sizeof(out_cfg->rabbitmq.password), "%s", rmq_pass);
    snprintf(out_cfg->rabbitmq.vhost, sizeof(out_cfg->rabbitmq.vhost), "%s", rmq_vhost);
    snprintf(out_cfg->rabbitmq.contactinfo_queue, sizeof(out_cfg->rabbitmq.contactinfo_queue), "%s", rmq_queue);
    out_cfg->rabbitmq.port = (int)rmq_port;

    /* ---- mysql ---- */
    const json_value_t *sql = json_object_get(root, "mysql");
    if (!sql || sql->type != JSON_OBJECT) {
        snprintf(errbuf, errbuf_size, "missing 'mysql' object");
        json_free(root);
        return -1;
    }
    const char *sql_host = json_get_string(sql, "host", NULL);
    const char *sql_user = json_get_string(sql, "username", NULL);
    const char *sql_pass = json_get_string(sql, "password", NULL);
    const char *sql_db = json_get_string(sql, "database", NULL);
    const char *sql_table = json_get_string(sql, "table", NULL);
    double sql_port = json_get_number(sql, "port", 3306);

    if (!sql_host || !sql_user || !sql_pass || !sql_db || !sql_table) {
        snprintf(errbuf, errbuf_size, "'mysql' requires 'host', 'username', 'password', 'database', and 'table'");
        json_free(root);
        return -1;
    }
    if (sql_port <= 0 || sql_port > 65535) {
        snprintf(errbuf, errbuf_size, "'mysql.port' must be between 1 and 65535");
        json_free(root);
        return -1;
    }

    snprintf(out_cfg->mysql.host, sizeof(out_cfg->mysql.host), "%s", sql_host);
    snprintf(out_cfg->mysql.username, sizeof(out_cfg->mysql.username), "%s", sql_user);
    snprintf(out_cfg->mysql.password, sizeof(out_cfg->mysql.password), "%s", sql_pass);
    snprintf(out_cfg->mysql.database, sizeof(out_cfg->mysql.database), "%s", sql_db);
    snprintf(out_cfg->mysql.table, sizeof(out_cfg->mysql.table), "%s", sql_table);
    out_cfg->mysql.port = (int)sql_port;

    const json_value_t *verify_cert_v = json_object_get(sql, "verify_cert");
    out_cfg->mysql.verify_cert = 1; /* default: verify (secure) */
    if (verify_cert_v) {
        if (verify_cert_v->type != JSON_BOOL) {
            snprintf(errbuf, errbuf_size, "'mysql.verify_cert' must be a boolean (true/false)");
            json_free(root);
            return -1;
        }
        out_cfg->mysql.verify_cert = verify_cert_v->u.boolean;
    }

    /* ---- field_map (optional) ---- */
    const json_value_t *fmap = json_object_get(root, "field_map");
    out_cfg->field_map_count = 0;
    if (fmap) {
        if (fmap->type != JSON_OBJECT) {
            snprintf(errbuf, errbuf_size, "'field_map' must be an object");
            json_free(root);
            return -1;
        }
        for (size_t i = 0; i < fmap->u.object.count && out_cfg->field_map_count < MAX_FIELD_MAP_ENTRIES; i++) {
            const char *k = fmap->u.object.keys[i];
            const json_value_t *v = fmap->u.object.values[i];
            if (v->type != JSON_STRING) {
                snprintf(errbuf, errbuf_size, "field_map['%s'] must be a string", k);
                json_free(root);
                return -1;
            }
            field_map_entry_t *e = &out_cfg->field_map[out_cfg->field_map_count++];
            snprintf(e->xml_field, sizeof(e->xml_field), "%s", k);
            snprintf(e->db_column, sizeof(e->db_column), "%s", v->u.string);
        }
    }

    /* ---- process_limit (optional) ---- */
    double process_limit = json_get_number(root, "process_limit", 0);
    if (process_limit < 0) {
        snprintf(errbuf, errbuf_size, "'process_limit' must not be negative");
        json_free(root);
        return -1;
    }
    out_cfg->process_limit = (long)process_limit;

    json_free(root);
    return 0;
}

const char *config_resolve_field(const app_config_t *cfg, const char *xml_field) {
    for (size_t i = 0; i < cfg->field_map_count; i++) {
        if (strcmp(cfg->field_map[i].xml_field, xml_field) == 0) {
            if (str_ci_equals(cfg->field_map[i].db_column, "null")) return NULL;
            return cfg->field_map[i].db_column;
        }
    }
    return xml_field; /* default: same-name column */
}
