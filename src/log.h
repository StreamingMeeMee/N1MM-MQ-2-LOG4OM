#ifndef N1MMMQ2LOG4OM_LOG_H
#define N1MMMQ2LOG4OM_LOG_H

/* Must be called once before log_message(); sets whether verbose output
 * (-v) is enabled. When disabled, log_message() prints nothing. */
void log_init(int verbose);

/* Prints one verbose-mode line: timestamp, N1MM message type, and a status
 * (e.g. the target table, "ignored", "dropped", "requeued"). No-op when
 * not verbose. */
void log_message(const char *msg_type, const char *status);

#endif /* N1MMMQ2LOG4OM_LOG_H */
