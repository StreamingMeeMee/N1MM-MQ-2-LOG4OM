#ifndef N1MMMQ2LOG4OM_MQ_CONSUMER_H
#define N1MMMQ2LOG4OM_MQ_CONSUMER_H

#include <amqp.h>
#include "config.h"

typedef struct {
    amqp_connection_state_t conn;
    int connected;
    char host[256];
    int port;
    char username[128];
    char password[128];
    char vhost[128];
    char queue[128];
} mq_consumer_t;

void mq_consumer_init(mq_consumer_t *c, const rabbitmq_config_t *cfg);

/* Opens the TCP connection, logs in, opens channel 1, idempotently declares
 * the configured queue (durable), and starts consuming from it. */
int mq_consumer_connect(mq_consumer_t *c, char *errbuf, size_t errbuf_len);

void mq_consumer_disconnect(mq_consumer_t *c);

/* Waits up to timeout_ms for one message. Returns 1 with *out_envelope set
 * (caller must amqp_destroy_envelope() it when done) if a message arrived,
 * 0 on timeout (no message), or -1 on a connection-level error (caller
 * should treat the consumer as disconnected). */
int mq_consumer_receive(mq_consumer_t *c, amqp_envelope_t *out_envelope, int timeout_ms,
                         char *errbuf, size_t errbuf_len);

int mq_consumer_ack(mq_consumer_t *c, uint64_t delivery_tag);
int mq_consumer_nack(mq_consumer_t *c, uint64_t delivery_tag, int requeue);

#endif /* N1MMMQ2LOG4OM_MQ_CONSUMER_H */
