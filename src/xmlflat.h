#ifndef N1MMMQ2LOG4OM_XMLFLAT_H
#define N1MMMQ2LOG4OM_XMLFLAT_H

#include <stddef.h>

#define XMLFLAT_MAX_ROOT_LEN 64
#define XMLFLAT_MAX_FIELD_NAME_LEN 64
#define XMLFLAT_MAX_FIELDS 256

typedef struct {
    char name[XMLFLAT_MAX_FIELD_NAME_LEN];
    char *value; /* heap-allocated, entity-unescaped text; owned by the doc */
} xmlflat_field_t;

typedef struct {
    char root[XMLFLAT_MAX_ROOT_LEN];
    xmlflat_field_t fields[XMLFLAT_MAX_FIELDS];
    size_t field_count;
} xmlflat_doc_t;

/*
 * Parses an N1MM-style flat XML message: a root element containing only
 * simple <name>text</name> children (no nesting). `buf`/`len` need not be
 * NUL-terminated. Skips an optional BOM, "<?xml ...?>" prolog, comments,
 * and DOCTYPE before the root, and unescapes XML entities
 * (&amp; &lt; &gt; &apos; &quot; &#NN; &#xHH;) in field text.
 *
 * Returns 0 on success (out->root and out->fields are populated) or -1 if
 * no root element could be found. On success the caller must call
 * xmlflat_free() to release field value memory.
 */
int xmlflat_parse(const char *buf, size_t len, xmlflat_doc_t *out);
void xmlflat_free(xmlflat_doc_t *doc);

/* Case-sensitive lookup of a child field's text by name, or NULL. */
const char *xmlflat_get(const xmlflat_doc_t *doc, const char *name);

#endif /* N1MMMQ2LOG4OM_XMLFLAT_H */
