#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *p;
    const char *begin;
    char *errbuf;
    size_t errbuf_size;
    int error;
    int depth;
} parser_t;

#define JSON_MAX_DEPTH 32

static void set_error(parser_t *ps, const char *msg) {
    if (ps->error) return;
    ps->error = 1;
    if (ps->errbuf && ps->errbuf_size > 0) {
        long offset = (long)(ps->p - ps->begin);
        snprintf(ps->errbuf, ps->errbuf_size, "%s at offset %ld", msg, offset);
    }
}

static void skip_ws(parser_t *ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
}

static json_value_t *new_value(json_type_t type) {
    json_value_t *v = (json_value_t *)calloc(1, sizeof(json_value_t));
    v->type = type;
    return v;
}

static json_value_t *parse_value(parser_t *ps);

static int parse_hex4(const char *p, unsigned *out) {
    unsigned val = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') val |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = val;
    return 0;
}

static void append_utf8(char **buf, size_t *len, size_t *cap, unsigned cp) {
    char tmp[4];
    int n;
    if (cp <= 0x7F) {
        tmp[0] = (char)cp;
        n = 1;
    } else if (cp <= 0x7FF) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp <= 0xFFFF) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (*len + (size_t)n + 1 > *cap) {
        *cap = (*len + (size_t)n + 1) * 2;
        *buf = (char *)realloc(*buf, *cap);
    }
    memcpy(*buf + *len, tmp, (size_t)n);
    *len += (size_t)n;
    (*buf)[*len] = '\0';
}

static void buf_push(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 2 > *cap) {
        *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static char *parse_string_raw(parser_t *ps) {
    /* assumes *ps->p == '"' */
    ps->p++;
    size_t cap = 32, len = 0;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';

    while (*ps->p != '"') {
        if (*ps->p == '\0') {
            set_error(ps, "unterminated string");
            free(buf);
            return NULL;
        }
        unsigned char c = (unsigned char)*ps->p;
        if (c != '\\') {
            buf_push(&buf, &len, &cap, (char)c);
            ps->p++;
            continue;
        }

        ps->p++; /* consume backslash */
        char esc = *ps->p;
        if (esc == '"' || esc == '\\' || esc == '/') {
            buf_push(&buf, &len, &cap, esc);
            ps->p++;
        } else if (esc == 'b') {
            buf_push(&buf, &len, &cap, '\b');
            ps->p++;
        } else if (esc == 'f') {
            buf_push(&buf, &len, &cap, '\f');
            ps->p++;
        } else if (esc == 'n') {
            buf_push(&buf, &len, &cap, '\n');
            ps->p++;
        } else if (esc == 'r') {
            buf_push(&buf, &len, &cap, '\r');
            ps->p++;
        } else if (esc == 't') {
            buf_push(&buf, &len, &cap, '\t');
            ps->p++;
        } else if (esc == 'u') {
            unsigned cp;
            if (parse_hex4(ps->p + 1, &cp) != 0) {
                set_error(ps, "invalid \\u escape");
                free(buf);
                return NULL;
            }
            ps->p += 5;
            if (cp >= 0xD800 && cp <= 0xDBFF && ps->p[0] == '\\' && ps->p[1] == 'u') {
                unsigned lo;
                if (parse_hex4(ps->p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ps->p += 6;
                }
            }
            append_utf8(&buf, &len, &cap, cp);
        } else {
            set_error(ps, "invalid escape sequence");
            free(buf);
            return NULL;
        }
    }
    ps->p++; /* closing quote */
    return buf;
}

static json_value_t *parse_object(parser_t *ps) {
    json_value_t *v = new_value(JSON_OBJECT);
    ps->p++; /* { */
    skip_ws(ps);
    size_t cap = 4;
    v->u.object.keys = (char **)malloc(sizeof(char *) * cap);
    v->u.object.values = (json_value_t **)malloc(sizeof(json_value_t *) * cap);
    v->u.object.count = 0;

    if (*ps->p == '}') {
        ps->p++;
        return v;
    }

    for (;;) {
        skip_ws(ps);
        if (*ps->p != '"') {
            set_error(ps, "expected string key");
            json_free(v);
            return NULL;
        }
        char *key = parse_string_raw(ps);
        if (!key) {
            json_free(v);
            return NULL;
        }
        skip_ws(ps);
        if (*ps->p != ':') {
            set_error(ps, "expected ':'");
            free(key);
            json_free(v);
            return NULL;
        }
        ps->p++;
        skip_ws(ps);
        json_value_t *val = parse_value(ps);
        if (!val) {
            free(key);
            json_free(v);
            return NULL;
        }

        if (v->u.object.count >= cap) {
            cap *= 2;
            v->u.object.keys = (char **)realloc(v->u.object.keys, sizeof(char *) * cap);
            v->u.object.values = (json_value_t **)realloc(v->u.object.values, sizeof(json_value_t *) * cap);
        }
        v->u.object.keys[v->u.object.count] = key;
        v->u.object.values[v->u.object.count] = val;
        v->u.object.count++;

        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            break;
        }
        set_error(ps, "expected ',' or '}'");
        json_free(v);
        return NULL;
    }
    return v;
}

static json_value_t *parse_array(parser_t *ps) {
    json_value_t *v = new_value(JSON_ARRAY);
    ps->p++; /* [ */
    skip_ws(ps);
    size_t cap = 4;
    v->u.array.items = (json_value_t **)malloc(sizeof(json_value_t *) * cap);
    v->u.array.count = 0;

    if (*ps->p == ']') {
        ps->p++;
        return v;
    }

    for (;;) {
        skip_ws(ps);
        json_value_t *val = parse_value(ps);
        if (!val) {
            json_free(v);
            return NULL;
        }
        if (v->u.array.count >= cap) {
            cap *= 2;
            v->u.array.items = (json_value_t **)realloc(v->u.array.items, sizeof(json_value_t *) * cap);
        }
        v->u.array.items[v->u.array.count++] = val;
        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            break;
        }
        set_error(ps, "expected ',' or ']'");
        json_free(v);
        return NULL;
    }
    return v;
}

static json_value_t *parse_value(parser_t *ps) {
    skip_ws(ps);
    char c = *ps->p;
    if (c == '{' || c == '[') {
        /* Bounded so a hostile/garbage payload of "[[[[..." can't overflow the stack. */
        if (ps->depth >= JSON_MAX_DEPTH) {
            set_error(ps, "nesting too deep");
            return NULL;
        }
        ps->depth++;
        json_value_t *nested = (c == '{') ? parse_object(ps) : parse_array(ps);
        ps->depth--;
        return nested;
    }
    if (c == '"') {
        char *s = parse_string_raw(ps);
        if (!s) return NULL;
        json_value_t *v = new_value(JSON_STRING);
        v->u.string = s;
        return v;
    }
    if (c == 't' && strncmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        json_value_t *v = new_value(JSON_BOOL);
        v->u.boolean = 1;
        return v;
    }
    if (c == 'f' && strncmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        json_value_t *v = new_value(JSON_BOOL);
        v->u.boolean = 0;
        return v;
    }
    if (c == 'n' && strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return new_value(JSON_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        double d = strtod(ps->p, &end);
        if (end == ps->p) {
            set_error(ps, "invalid number");
            return NULL;
        }
        ps->p = end;
        json_value_t *v = new_value(JSON_NUMBER);
        v->u.number = d;
        return v;
    }
    set_error(ps, "unexpected character");
    return NULL;
}

json_value_t *json_parse(const char *text, char *errbuf, size_t errbuf_size) {
    parser_t ps;
    ps.p = text;
    ps.begin = text;
    ps.errbuf = errbuf;
    ps.errbuf_size = errbuf_size;
    ps.error = 0;
    ps.depth = 0;

    json_value_t *v = parse_value(&ps);
    if (!v) return NULL;
    skip_ws(&ps);
    if (*ps.p != '\0') {
        set_error(&ps, "trailing data after JSON value");
        json_free(v);
        return NULL;
    }
    return v;
}

void json_free(json_value_t *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->u.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.keys[i]);
                json_free(v->u.object.values[i]);
            }
            free(v->u.object.keys);
            free(v->u.object.values);
            break;
        default:
            break;
    }
    free(v);
}

const json_value_t *json_object_get(const json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0) return obj->u.object.values[i];
    }
    return NULL;
}

const char *json_get_string(const json_value_t *obj, const char *key, const char *default_val) {
    const json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.string;
    return default_val;
}

double json_get_number(const json_value_t *obj, const char *key, double default_val) {
    const json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->u.number;
    return default_val;
}
