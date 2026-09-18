#ifndef N1MMMQ2LOG4OM_LOG_H
#define N1MMMQ2LOG4OM_LOG_H

#include <stddef.h>

/* Must be called once before log_message(); sets whether verbose output
 * (-v) is enabled. When disabled, log_message() prints nothing. */
void log_init(int verbose);

/* Prints one verbose-mode line: timestamp, N1MM message type, and a status
 * (e.g. "upserted", "requeued (...)", "discarded (...)"). No-op when
 * not verbose. */
void log_message(const char *msg_type, const char *status);

/* Prints a received message's raw payload on its own line (byte count, then
 * the content with non-printable bytes escaped as \xNN, capped at 8192
 * bytes). Used to show what a malformed message actually contained. No-op
 * when not verbose. */
void log_payload(const char *body, size_t len);

#endif /* N1MMMQ2LOG4OM_LOG_H */
