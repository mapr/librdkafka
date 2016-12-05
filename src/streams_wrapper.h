#ifndef STREAMS_WRAPPER_H
#define STREAMS_WRAPPER_H

#include "rdkafka.h"

typedef struct {
	rd_kafka_t *rk;
	void *msg_opaque;
	rd_kafka_topic_t *topic;
	int msgflags;
} streams_producer_callback_ctx;


typedef struct {
	rd_kafka_t *rk;
} streams_consumer_callback_ctx;

typedef struct {
	rd_kafka_t *rk;
	rd_kafka_resp_err_t err;
	void *opaque;
} streams_offset_commit_callback_ctx;

#endif


