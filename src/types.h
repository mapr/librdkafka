/* Copyright (c) 2015 & onwards. MapR Tech, Inc., All rights reserved */

#ifndef STREAMS_API_TYPES_H_
#define STREAMS_API_TYPES_H_

#include <stddef.h>
#include <stdint.h>
#ifndef _MSC_VER
#include <stdbool.h>
#endif
#include <errno.h>

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAMS_INVALID_SEQNUM -1
#define STREAMS_INVALID_PARTITION_ID -1

typedef void *streams_config_t;
typedef void *streams_topic_partition_t;

typedef void *streams_consumer_t;
typedef void *streams_consumer_record_t;

typedef void *streams_producer_t;
typedef void *streams_producer_record_t;

typedef void *streams_assign_list_t;


typedef void (*streams_rebalance_cb) (
  streams_topic_partition_t *topic_partitions,
  uint32_t topic_partitions_size,
  void *cb_ctx);

typedef void (*streams_commit_cb) (
  streams_topic_partition_t *topic_partitions,
  int64_t *offsets,
  uint32_t topic_partitions_size,
  int32_t err,
  void *cb_ctx);

typedef void (*streams_producer_cb)(
    int32_t err,
    streams_producer_record_t record,
    int partitionid,
    int64_t offset,
    void *cb_ctx);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* STREAMS_API_TYPES_H_ */
