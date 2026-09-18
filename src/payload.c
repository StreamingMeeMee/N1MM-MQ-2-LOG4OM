#include "payload.h"

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

/* Returns a malloc'd string for a JSON scalar (or an empty container), or
 * NULL if the value is a non-empty object/array, which a flat message
 * can't represent. */
static char *json_value_to_field_text(const json_value_t *v) {
    switch (v->type) {
        case JSON_STRING:
            return dup_cstr(v->u.string);
        case JSON_NUMBER: {
            char tmp[64];
            double d = v->u.number;
            if (d > -1e15 && d < 1e15 && (double)(long long)d == d) {
                snprintf(tmp, sizeof(tmp), "%lld", (long long)d);
            } else {
                snprintf(tmp, sizeof(tmp), "%.15g", d);
            }
            return dup_cstr(tmp);
        }
        case JSON_BOOL:
            return dup_cstr(v->u.boolean ? "1" : "0");
        case JSON_NULL:
            return dup_cstr("");
        case JSON_OBJECT:
            return v->u.object.count == 0 ? dup_cstr("") : NULL;
        case JSON_ARRAY:
            return v->u.array.count == 0 ? dup_cstr("") : NULL;
    }
    return NULL;
}

static int parse_json_body(const char *buf, size_t len, xmlflat_doc_t *out) {
    /* json_parse() wants NUL-terminated text, and would silently stop at an
     * embedded NUL and accept a truncated prefix, so reject those outright. */
    if (memchr(buf, '\0', len) != NULL) return -1;

    char *text = (char *)malloc(len + 1);
    memcpy(text, buf, len);
    text[len] = '\0';

    char jerr[128];
    json_value_t *root = json_parse(text, jerr, sizeof(jerr));
    free(text);
    if (!root) return -1;
    if (root->type != JSON_OBJECT) {
        json_free(root);
        return -1;
    }

    memcpy(out->root, "contactinfo", sizeof("contactinfo"));

    int rc = 0;
    for (size_t i = 0; i < root->u.object.count; i++) {
        const char *name = root->u.object.keys[i];
        char *value = json_value_to_field_text(root->u.object.values[i]);
        if (!value) {
            rc = -1;
            break;
        }
        size_t name_len = strlen(name);
        /* Same limits (and same silent skip) as the XML path. */
        if (out->field_count < XMLFLAT_MAX_FIELDS && name_len > 0 && name_len < XMLFLAT_MAX_FIELD_NAME_LEN) {
            memcpy(out->fields[out->field_count].name, name, name_len + 1);
            out->fields[out->field_count].value = value;
            out->field_count++;
        } else {
            free(value);
        }
    }

    json_free(root);
    return rc;
}

int payload_parse(const char *buf, size_t len, xmlflat_doc_t *out) {
    memset(out, 0, sizeof(*out));

    size_t i = 0;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        i = 3;
    }
    while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;

    if (i < len && buf[i] == '{') {
        return parse_json_body(buf + i, len - i, out);
    }
    return xmlflat_parse(buf, len, out);
}
