#ifndef MARLIN_WRAPPER_H
#define MARLIN_WRAPPER_H

#include "rdkafka.h"

typedef struct producer_callback_ctx {
	        rd_kafka_t *rk;
		        void *msg_opaque;
			        rd_kafka_topic_t *topic;
				        int msgflags;
} marlin_producer_callback_ctx;


typedef struct consumer_callback_ctx {
	        rd_kafka_t *rk;
		        rd_kafka_topic_partition_list_t *partitions;
} marlin_consumer_callback_ctx;

#endif


