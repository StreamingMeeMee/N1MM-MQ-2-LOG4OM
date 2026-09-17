#include "xmlflat.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_name_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

static int is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.';
}

/* Advances *ip past whitespace, "<?...?>" prologs, "<!--...-->" comments,
 * and "<!DOCTYPE ...>" until it points at a real '<' (element open or
 * close). Returns 0 on success, -1 if the buffer ends first. */
static int skip_junk(const char *buf, size_t len, size_t *ip) {
    size_t i = *ip;
    for (;;) {
        while (i < len && isspace((unsigned char)buf[i])) i++;
        if (i >= len) {
            *ip = i;
            return -1;
        }
        if (buf[i] != '<') {
            *ip = i;
            return -1; /* stray text where a tag was expected */
        }
        if (i + 1 < len && buf[i + 1] == '?') {
            size_t j = i + 2;
            while (j + 1 < len && !(buf[j] == '?' && buf[j + 1] == '>')) j++;
            if (j + 1 >= len) {
                *ip = i;
                return -1;
            }
            i = j + 2;
            continue;
        }
        if (i + 3 < len && buf[i + 1] == '!' && buf[i + 2] == '-' && buf[i + 3] == '-') {
            size_t j = i + 4;
            while (j + 2 < len && !(buf[j] == '-' && buf[j + 1] == '-' && buf[j + 2] == '>')) j++;
            if (j + 2 >= len) {
                *ip = i;
                return -1;
            }
            i = j + 3;
            continue;
        }
        if (i + 1 < len && buf[i + 1] == '!') {
            size_t j = i + 2;
            while (j < len && buf[j] != '>') j++;
            if (j >= len) {
                *ip = i;
                return -1;
            }
            i = j + 1;
            continue;
        }
        *ip = i;
        return 0;
    }
}

/* Called with *ip just past a tag's name; advances past any attributes
 * (quote-aware) to just after '>' or "/>". Sets *self_closing for "/>". */
static int skip_tag_tail(const char *buf, size_t len, size_t *ip, int *self_closing) {
    size_t i = *ip;
    *self_closing = 0;
    while (i < len) {
        char c = buf[i];
        if (c == '>') {
            *ip = i + 1;
            return 0;
        }
        if (c == '/' && i + 1 < len && buf[i + 1] == '>') {
            *self_closing = 1;
            *ip = i + 2;
            return 0;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            i++;
            while (i < len && buf[i] != quote) i++;
            if (i >= len) return -1;
            i++;
            continue;
        }
        i++;
    }
    return -1;
}

static void ubuf_push(char **buf, size_t *l, size_t *cap, char c) {
    if (*l + 2 > *cap) {
        *cap = (*cap == 0 ? 16 : *cap * 2);
        *buf = (char *)realloc(*buf, *cap);
    }
    (*buf)[(*l)++] = c;
    (*buf)[*l] = '\0';
}

static void ubuf_push_utf8(char **buf, size_t *l, size_t *cap, unsigned cp) {
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
    if (*l + (size_t)n + 1 > *cap) {
        *cap = (*l + (size_t)n + 1) * 2;
        *buf = (char *)realloc(*buf, *cap);
    }
    memcpy(*buf + *l, tmp, (size_t)n);
    *l += (size_t)n;
    (*buf)[*l] = '\0';
}

/* Decodes XML entities (&amp; &lt; &gt; &apos; &quot; &#NN; &#xHH;) in
 * src[0..len). Unrecognized "&...;" sequences and lone '&' are copied
 * through literally rather than treated as errors. */
static char *xml_unescape(const char *src, size_t len) {
    size_t cap = len + 16;
    char *buf = (char *)malloc(cap);
    size_t l = 0;
    buf[0] = '\0';

    size_t i = 0;
    while (i < len) {
        if (src[i] != '&') {
            ubuf_push(&buf, &l, &cap, src[i]);
            i++;
            continue;
        }

        size_t j = i + 1;
        while (j < len && j < i + 12 && src[j] != ';') j++;
        if (j < len && src[j] == ';') {
            const char *ent = src + i + 1;
            size_t entlen = j - i - 1;
            if (entlen == 3 && strncmp(ent, "amp", 3) == 0) {
                ubuf_push(&buf, &l, &cap, '&');
                i = j + 1;
                continue;
            }
            if (entlen == 2 && strncmp(ent, "lt", 2) == 0) {
                ubuf_push(&buf, &l, &cap, '<');
                i = j + 1;
                continue;
            }
            if (entlen == 2 && strncmp(ent, "gt", 2) == 0) {
                ubuf_push(&buf, &l, &cap, '>');
                i = j + 1;
                continue;
            }
            if (entlen == 4 && strncmp(ent, "apos", 4) == 0) {
                ubuf_push(&buf, &l, &cap, '\'');
                i = j + 1;
                continue;
            }
            if (entlen == 4 && strncmp(ent, "quot", 4) == 0) {
                ubuf_push(&buf, &l, &cap, '"');
                i = j + 1;
                continue;
            }
            if (entlen >= 2 && ent[0] == '#') {
                unsigned cp = 0;
                int ok = 1;
                if (ent[1] == 'x' || ent[1] == 'X') {
                    if (entlen < 3) ok = 0;
                    for (size_t k = 2; ok && k < entlen; k++) {
                        char ch = ent[k];
                        cp <<= 4;
                        if (ch >= '0' && ch <= '9') cp |= (unsigned)(ch - '0');
                        else if (ch >= 'a' && ch <= 'f') cp |= (unsigned)(ch - 'a' + 10);
                        else if (ch >= 'A' && ch <= 'F') cp |= (unsigned)(ch - 'A' + 10);
                        else ok = 0;
                    }
                } else {
                    for (size_t k = 1; ok && k < entlen; k++) {
                        char ch = ent[k];
                        if (ch < '0' || ch > '9') {
                            ok = 0;
                            break;
                        }
                        cp = cp * 10 + (unsigned)(ch - '0');
                    }
                }
                if (ok) {
                    ubuf_push_utf8(&buf, &l, &cap, cp);
                    i = j + 1;
                    continue;
                }
            }
        }

        ubuf_push(&buf, &l, &cap, '&');
        i++;
    }
    return buf;
}

int xmlflat_parse(const char *buf, size_t len, xmlflat_doc_t *out) {
    memset(out, 0, sizeof(*out));
    size_t i = 0;

    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        i += 3;
    }

    if (skip_junk(buf, len, &i) != 0) return -1;

    /* root open tag */
    i++; /* consume '<' */
    if (i >= len || !is_name_start_char(buf[i])) return -1;
    size_t name_start = i;
    while (i < len && is_name_char(buf[i])) i++;
    size_t name_len = i - name_start;
    if (name_len == 0 || name_len >= sizeof(out->root)) return -1;
    memcpy(out->root, buf + name_start, name_len);
    out->root[name_len] = '\0';

    int root_self_closing = 0;
    if (skip_tag_tail(buf, len, &i, &root_self_closing) != 0) return -1;
    if (root_self_closing) return 0;

    for (;;) {
        if (skip_junk(buf, len, &i) != 0) return -1;

        if (i + 1 < len && buf[i + 1] == '/') {
            /* closing tag for root (name not re-verified) */
            size_t j = i + 2;
            while (j < len && buf[j] != '>') j++;
            if (j >= len) return -1;
            i = j + 1;
            break;
        }

        i++; /* consume '<' */
        if (i >= len || !is_name_start_char(buf[i])) return -1;
        size_t cname_start = i;
        while (i < len && is_name_char(buf[i])) i++;
        size_t cname_len = i - cname_start;

        int child_self_closing = 0;
        if (skip_tag_tail(buf, len, &i, &child_self_closing) != 0) return -1;

        char *value;
        if (child_self_closing) {
            value = (char *)malloc(1);
            value[0] = '\0';
        } else {
            size_t text_start = i;
            while (i < len && buf[i] != '<') i++;
            if (i >= len) return -1;
            value = xml_unescape(buf + text_start, i - text_start);

            if (i + 1 < len && buf[i + 1] == '/') {
                size_t j = i + 2;
                while (j < len && buf[j] != '>') j++;
                if (j >= len) {
                    free(value);
                    return -1;
                }
                i = j + 1;
            } else {
                /* nested element under a leaf: not a flat message we
                 * understand, bail rather than mis-parse. */
                free(value);
                return -1;
            }
        }

        if (out->field_count < XMLFLAT_MAX_FIELDS && cname_len > 0 && cname_len < XMLFLAT_MAX_FIELD_NAME_LEN) {
            memcpy(out->fields[out->field_count].name, buf + cname_start, cname_len);
            out->fields[out->field_count].name[cname_len] = '\0';
            out->fields[out->field_count].value = value;
            out->field_count++;
        } else {
            free(value);
        }
    }

    return 0;
}

void xmlflat_free(xmlflat_doc_t *doc) {
    if (!doc) return;
    for (size_t i = 0; i < doc->field_count; i++) {
        free(doc->fields[i].value);
        doc->fields[i].value = NULL;
    }
    doc->field_count = 0;
}

const char *xmlflat_get(const xmlflat_doc_t *doc, const char *name) {
    for (size_t i = 0; i < doc->field_count; i++) {
        if (strcmp(doc->fields[i].name, name) == 0) return doc->fields[i].value;
    }
    return NULL;
}
