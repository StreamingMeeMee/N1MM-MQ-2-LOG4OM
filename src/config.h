#ifndef N1MMMQ2LOG4OM_CONFIG_H
#define N1MMMQ2LOG4OM_CONFIG_H

#include <stddef.h>

#define MAX_FIELD_MAP_ENTRIES 128

typedef struct {
    char host[256];
    int port;
    char username[128];
    char password[128];
    char vhost[128];
    char queue[128];
} rabbitmq_config_t;

typedef struct {
    char host[256];
    int port;
    char username[128];
    char password[128];
    char database[128];
    char table[128];
    int verify_cert; /* default 1 (true): validate the server's TLS certificate chain */
} mysql_config_t;

typedef struct {
    char xml_field[64];
    char db_column[128];
} field_map_entry_t;

typedef struct {
    rabbitmq_config_t rabbitmq;
    mysql_config_t mysql;
    field_map_entry_t field_map[MAX_FIELD_MAP_ENTRIES];
    size_t field_map_count;
    long process_limit; /* 0 (default/absent) = unlimited */
} app_config_t;

/* Loads and validates the JSON config at `path`. Returns 0 on success,
 * -1 on failure with a human-readable message in errbuf. */
int config_load(const char *path, app_config_t *out_cfg, char *errbuf, size_t errbuf_size);

/* Resolves a parsed N1MM XML field name to a target DB column name: an
 * exact (case-sensitive) match in field_map wins; otherwise the default is
 * a column of the same name. Returns NULL if the resolved mapping is
 * "null" (case-insensitive), meaning this field should be dropped. The
 * returned pointer is either `xml_field` itself (default case) or owned by
 * `cfg` (mapped case) -- never allocated, nothing to free. */
const char *config_resolve_field(const app_config_t *cfg, const char *xml_field);

#endif /* N1MMMQ2LOG4OM_CONFIG_H */
