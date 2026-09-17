#ifndef N1MMMQ2LOG4OM_DB_CLIENT_H
#define N1MMMQ2LOG4OM_DB_CLIENT_H

#include <mysql.h>
#include "config.h"

#define DB_MAX_COLUMNS 256
#define DB_COLUMN_NAME_LEN 128

typedef struct {
    char name[DB_COLUMN_NAME_LEN];
    long max_length; /* character_maximum_length, or -1 if not a length-bounded type */
} db_column_t;

typedef struct {
    MYSQL *conn;
    int connected;
    char host[256];
    int port;
    char username[128];
    char password[128];
    char database[128];
    char table[128];
    db_column_t columns[DB_MAX_COLUMNS];
    size_t column_count;
} db_client_t;

void db_client_init(db_client_t *db, const mysql_config_t *cfg);

/* Connects and discovers the configured table's columns (name and
 * character_maximum_length) via information_schema.columns. */
int db_client_connect(db_client_t *db, char *errbuf, size_t errbuf_len);

void db_client_disconnect(db_client_t *db);

/* Case-insensitive lookup of a discovered column by name. Returns 1 and
 * sets *out_max_len (-1 if the column has no length limit, e.g. numeric/
 * date types) if found, 0 otherwise. */
int db_client_has_column(const db_client_t *db, const char *name, long *out_max_len);

typedef struct {
    const char *column;
    const char *value; /* NULL means SQL NULL */
} db_field_t;

/* Builds and executes "INSERT INTO <table> (...) VALUES (...) ON DUPLICATE
 * KEY UPDATE ..." as a prepared statement, binding every non-NULL value as
 * a string (MySQL coerces to each column's real type). Returns 0 on
 * success. On failure, *out_is_connection_error distinguishes a
 * connection-level problem (caller should reconnect and retry the message)
 * from a query/data-level error (caller should give up on this message). */
int db_client_upsert(db_client_t *db, const db_field_t *fields, size_t field_count,
                      int *out_is_connection_error, char *errbuf, size_t errbuf_len);

#endif /* N1MMMQ2LOG4OM_DB_CLIENT_H */
