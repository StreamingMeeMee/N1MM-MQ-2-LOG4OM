/*
 * Minimal JSON parser sufficient for this app's fixed config schema
 * (objects, arrays, strings, numbers, booleans, null). Not a general-purpose
 * JSON library.
 */
#ifndef N1MMMQ2LOG4OM_JSON_H
#define N1MMMQ2LOG4OM_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value_t;

struct json_value {
    json_type_t type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            json_value_t **items;
            size_t count;
        } array;
        struct {
            char **keys;
            json_value_t **values;
            size_t count;
        } object;
    } u;
};

/* Parses `text` (NUL-terminated). Returns NULL and fills errbuf on failure.
 * Caller owns the returned tree and must free it with json_free(). */
json_value_t *json_parse(const char *text, char *errbuf, size_t errbuf_size);
void json_free(json_value_t *v);

const json_value_t *json_object_get(const json_value_t *obj, const char *key);
const char *json_get_string(const json_value_t *obj, const char *key, const char *default_val);
double json_get_number(const json_value_t *obj, const char *key, double default_val);

#endif /* N1MMMQ2LOG4OM_JSON_H */
