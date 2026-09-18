#ifndef N1MMMQ2LOG4OM_PAYLOAD_H
#define N1MMMQ2LOG4OM_PAYLOAD_H

#include <stddef.h>
#include "xmlflat.h"

/*
 * Parses a contactinfo message body into a flat (name, value) field list,
 * accepting either format seen on the queue:
 *   - a flat JSON object, {"call":"W1AW","comment":{},...}: string values
 *     are used as-is, numbers are formatted as plain decimals, true/false
 *     become 1/0, and null or an empty {} / [] becomes an empty string
 *     (matching how an empty XML element is handled);
 *   - N1MM's flat XML (see xmlflat_parse).
 * The format is chosen by the first non-blank character: '{' means JSON,
 * anything else is tried as XML.
 *
 * Returns 0 on success, -1 if the body is neither valid JSON nor valid XML
 * (including a JSON value that isn't a flat object, or a field whose value
 * is a non-empty nested object/array). Whether it succeeds or not, the
 * caller must call xmlflat_free(out) afterwards.
 */
int payload_parse(const char *buf, size_t len, xmlflat_doc_t *out);

#endif /* N1MMMQ2LOG4OM_PAYLOAD_H */
