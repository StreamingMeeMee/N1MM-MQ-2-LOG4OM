#include "db_client.h"

#include <errmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp_portable _stricmp
#else
#include <strings.h>
#define strcasecmp_portable strcasecmp
#endif

static int is_connection_error(unsigned int err) {
    switch (err) {
        case CR_SERVER_GONE_ERROR:
        case CR_SERVER_LOST:
        case CR_CONNECTION_ERROR:
        case CR_CONN_HOST_ERROR:
        case CR_UNKNOWN_HOST:
            return 1;
        default:
            return 0;
    }
}

void db_client_init(db_client_t *db, const mysql_config_t *cfg) {
    memset(db, 0, sizeof(*db));
    snprintf(db->host, sizeof(db->host), "%s", cfg->host);
    db->port = cfg->port;
    snprintf(db->username, sizeof(db->username), "%s", cfg->username);
    snprintf(db->password, sizeof(db->password), "%s", cfg->password);
    snprintf(db->database, sizeof(db->database), "%s", cfg->database);
    snprintf(db->table, sizeof(db->table), "%s", cfg->table);
    db->verify_cert = cfg->verify_cert;
}

int db_client_connect(db_client_t *db, char *errbuf, size_t errbuf_len) {
    db->conn = mysql_init(NULL);
    if (!db->conn) {
        snprintf(errbuf, errbuf_len, "mysql_init failed");
        return -1;
    }

    /* Controls whether the server's TLS certificate chain is validated
     * (the connection itself is still encrypted either way if the server
     * offers TLS). Off by request, for servers with a self-signed or
     * otherwise untrusted cert on a private network. */
    my_bool verify = (my_bool)(db->verify_cert ? 1 : 0);
    mysql_options(db->conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);

    if (!mysql_real_connect(db->conn, db->host, db->username, db->password, db->database,
                             (unsigned int)db->port, NULL, 0)) {
        snprintf(errbuf, errbuf_len, "cannot connect to %s:%d: %s", db->host, db->port, mysql_error(db->conn));
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    db->column_count = 0;

    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        snprintf(errbuf, errbuf_len, "mysql_stmt_init failed");
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    static const char *discover_sql =
        "SELECT column_name, character_maximum_length FROM information_schema.columns "
        "WHERE table_schema = ? AND table_name = ?";
    if (mysql_stmt_prepare(stmt, discover_sql, (unsigned long)strlen(discover_sql)) != 0) {
        snprintf(errbuf, errbuf_len, "column discovery prepare failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    unsigned long db_len = (unsigned long)strlen(db->database);
    unsigned long tbl_len = (unsigned long)strlen(db->table);
    MYSQL_BIND params[2];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (void *)db->database;
    params[0].buffer_length = db_len;
    params[0].length = &db_len;
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (void *)db->table;
    params[1].buffer_length = tbl_len;
    params[1].length = &tbl_len;

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        snprintf(errbuf, errbuf_len, "column discovery query failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    char name_buf[DB_COLUMN_NAME_LEN];
    unsigned long name_len = 0;
    my_bool name_is_null = 0;
    long long max_len_val = 0;
    my_bool max_len_is_null = 0;

    MYSQL_BIND results[2];
    memset(results, 0, sizeof(results));
    results[0].buffer_type = MYSQL_TYPE_STRING;
    results[0].buffer = name_buf;
    results[0].buffer_length = sizeof(name_buf);
    results[0].length = &name_len;
    results[0].is_null = &name_is_null;
    results[1].buffer_type = MYSQL_TYPE_LONGLONG;
    results[1].buffer = &max_len_val;
    results[1].is_null = &max_len_is_null;

    if (mysql_stmt_bind_result(stmt, results) != 0) {
        snprintf(errbuf, errbuf_len, "column discovery bind_result failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    while (mysql_stmt_fetch(stmt) == 0 && db->column_count < DB_MAX_COLUMNS) {
        size_t copy_len = name_len < sizeof(name_buf) - 1 ? name_len : sizeof(name_buf) - 1;
        db_column_t *col = &db->columns[db->column_count++];
        memcpy(col->name, name_buf, copy_len);
        col->name[copy_len] = '\0';
        col->max_length = max_len_is_null ? -1 : (long)max_len_val;
    }

    mysql_stmt_close(stmt);

    if (db->column_count == 0) {
        snprintf(errbuf, errbuf_len, "table '%s.%s' has no columns (does it exist?)", db->database, db->table);
        mysql_close(db->conn);
        db->conn = NULL;
        return -1;
    }

    db->connected = 1;
    return 0;
}

void db_client_disconnect(db_client_t *db) {
    if (db->conn) {
        mysql_close(db->conn);
        db->conn = NULL;
    }
    db->connected = 0;
}

int db_client_has_column(const db_client_t *db, const char *name, long *out_max_len) {
    for (size_t i = 0; i < db->column_count; i++) {
        if (strcasecmp_portable(db->columns[i].name, name) == 0) {
            if (out_max_len) *out_max_len = db->columns[i].max_length;
            return 1;
        }
    }
    return 0;
}

/* Minimal growable string, so the statement length is never guessed at. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int failed;
} sqlbuf_t;

static void sb_append(sqlbuf_t *sb, const char *s, size_t n) {
    if (sb->failed) return;
    if (sb->len + n + 1 > sb->cap) {
        size_t new_cap = sb->cap ? sb->cap : 256;
        while (new_cap < sb->len + n + 1) new_cap *= 2;
        char *grown = (char *)realloc(sb->data, new_cap);
        if (!grown) {
            sb->failed = 1;
            return;
        }
        sb->data = grown;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_puts(sqlbuf_t *sb, const char *s) {
    sb_append(sb, s, strlen(s));
}

/* Appends `name` as a backtick-quoted identifier (embedded backticks doubled). */
static void sb_ident(sqlbuf_t *sb, const char *name) {
    sb_append(sb, "`", 1);
    for (const char *p = name; *p; p++) {
        if (*p == '`') sb_append(sb, "``", 2);
        else sb_append(sb, p, 1);
    }
    sb_append(sb, "`", 1);
}

/* Builds "INSERT INTO `t` (`a`,`b`) VALUES (?,?) ON DUPLICATE KEY UPDATE
 * `a`=VALUES(`a`),`b`=VALUES(`b`)". Returns a malloc'd NUL-terminated string
 * (length in *out_len) or NULL on allocation failure. */
static char *build_upsert_sql(const char *table, const db_field_t *fields, size_t field_count, size_t *out_len) {
    sqlbuf_t sb = {0};

    sb_puts(&sb, "INSERT INTO ");
    sb_ident(&sb, table);
    sb_puts(&sb, " (");
    for (size_t i = 0; i < field_count; i++) {
        if (i) sb_append(&sb, ",", 1);
        sb_ident(&sb, fields[i].column);
    }
    sb_puts(&sb, ") VALUES (");
    for (size_t i = 0; i < field_count; i++) {
        if (i) sb_append(&sb, ",", 1);
        sb_append(&sb, "?", 1);
    }
    sb_puts(&sb, ") ON DUPLICATE KEY UPDATE ");
    for (size_t i = 0; i < field_count; i++) {
        if (i) sb_append(&sb, ",", 1);
        sb_ident(&sb, fields[i].column);
        sb_puts(&sb, "=VALUES(");
        sb_ident(&sb, fields[i].column);
        sb_append(&sb, ")", 1);
    }

    if (sb.failed) {
        free(sb.data);
        return NULL;
    }
    *out_len = sb.len;
    return sb.data;
}

int db_client_upsert(db_client_t *db, const db_field_t *fields, size_t field_count,
                      int *out_is_connection_error, char *errbuf, size_t errbuf_len) {
    *out_is_connection_error = 0;
    if (field_count == 0) {
        snprintf(errbuf, errbuf_len, "no fields to insert");
        return -1;
    }

    size_t pos = 0;
    char *sql = build_upsert_sql(db->table, fields, field_count, &pos);
    if (!sql) {
        snprintf(errbuf, errbuf_len, "out of memory building SQL");
        return -1;
    }

    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        snprintf(errbuf, errbuf_len, "mysql_stmt_init failed");
        free(sql);
        return -1;
    }

    if (mysql_stmt_prepare(stmt, sql, (unsigned long)pos) != 0) {
        snprintf(errbuf, errbuf_len, "prepare failed: %s", mysql_stmt_error(stmt));
        *out_is_connection_error = is_connection_error(mysql_stmt_errno(stmt));
        mysql_stmt_close(stmt);
        free(sql);
        return -1;
    }
    free(sql);

    MYSQL_BIND *binds = (MYSQL_BIND *)calloc(field_count, sizeof(MYSQL_BIND));
    unsigned long *lengths = (unsigned long *)calloc(field_count, sizeof(unsigned long));
    my_bool *is_null = (my_bool *)calloc(field_count, sizeof(my_bool));

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].value == NULL) {
            is_null[i] = 1;
            binds[i].buffer_type = MYSQL_TYPE_NULL;
            binds[i].is_null = &is_null[i];
        } else {
            lengths[i] = (unsigned long)strlen(fields[i].value);
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = (void *)fields[i].value;
            binds[i].buffer_length = lengths[i];
            binds[i].length = &lengths[i];
            binds[i].is_null = &is_null[i];
        }
    }

    int rc = 0;
    if (mysql_stmt_bind_param(stmt, binds) != 0) {
        snprintf(errbuf, errbuf_len, "bind_param failed: %s", mysql_stmt_error(stmt));
        *out_is_connection_error = is_connection_error(mysql_stmt_errno(stmt));
        rc = -1;
    } else if (mysql_stmt_execute(stmt) != 0) {
        snprintf(errbuf, errbuf_len, "execute failed: %s", mysql_stmt_error(stmt));
        *out_is_connection_error = is_connection_error(mysql_stmt_errno(stmt));
        rc = -1;
    }

    free(binds);
    free(lengths);
    free(is_null);
    mysql_stmt_close(stmt);
    return rc;
}
