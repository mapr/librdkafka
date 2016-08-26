/* Copyright (c) 2015 & onwards. MapR Tech, Inc., All rights reserved */

#ifndef STREAMS_COMMON_H__
#define STREAMS_COMMON_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_PARTITION_ID -1

/** \brief Create a topic partition object
  * \param topic_name [IN] : topic name
  * \param partition_id [IN] : partition id
  * \param tp [OUT] : topic partition returned
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Create a topic partition using the topic name and the partition id.
  * topic_name memory can be freed once the topic partition is
  * created.  If INVALID_PARTITION_ID is passed in, then streams
  * producer will assign a partition to the message either by a
  * key-based hashing or round robin.
  */
STREAMS_API int32_t
streams_topic_partition_create(const char *topic_name,
                               int partition_id,
                               streams_topic_partition_t *tp);

/** \brief Delete the topic partition
  * \param tp [IN] : topic partition to delete
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */
STREAMS_API int32_t
streams_topic_partition_destroy(streams_topic_partition_t tp);


/** \brief Get topic name from the topic partition
  * \param tp [IN] : topic partition
  * \param topic_name [OUT] : full topic name returned.
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Topic name returned is a pointer to a string inside the
  * streams_topic_partition_t object. This string should not be freed
  * explicitly.
  */
STREAMS_API int32_t
streams_topic_partition_get_topic_name(const streams_topic_partition_t tp,
                                       char **topic_name);

/** \brief Get partition id from the topic partition
  * \param tp [IN] : topic partition
  * \param partition_id [OUT] : partition id of this topic partition
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */
STREAMS_API int32_t
streams_topic_partition_get_partition_id(const streams_topic_partition_t tp,
                                         int *partition_id);

/** \brief Create a new Streams Config
  * \param config [OUT] : streams config returned
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */
STREAMS_API int32_t
streams_config_create(streams_config_t *config);


/** \brief Set a config parameter (key-value pair) in the config
  * \param config [IN] : streams config
  * \param name [IN] : key for the config parameter.
  * \param value [IN] : value for the config parameter
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Here is a list a valid config parameters accepted by the config.
  *
  * // Kafka client parameters supported by Streams
  *
  * "client.id": (default username) A user provided client id for producer or
  * consumer for debugging and tracking. If the client id is not
  * provided, Streams client will use the username as client id. For
  * producers, the client id will be used to tagg the records
  * generated by the producer.
  *
  * // Kafka producer parameters supported by Streams
  *
  * "buffer.memory": (default 33554432) The total bytes of memory the producer
  * can use to buffer records waiting to be sent to the server. If records are
  * sent faster than they can be delivered to the server the producer will
  * block.
  *
  * "metadata.max.age.ms": (default 600 * 1000) The producer generally refreshes
  * the topic metadata from the server when there is a failure. It will also
  * poll for this data regularly.
  *
  * // New producer config parameters for Streams
  * "streams.buffer.max.time.ms": (default 3 * 1000) Messages are buffered in
  * the producer for at most the specified time.  A thread will flush all the
  * messages that have been buffered for more than the time specified.

  * "streams.parallel.flushers.per.partition": (default true) If enabled,
  * producer may have multiple parallel send requests to the server for each
  * topic partition.  Note that if this setting is set to true, then there is a
  * risk of message re-ordering.
  *
  * // Kafka consumer parameters supported by Streams
  * "group.id": A string that uniquely identifies the group of consumer
  * processes to which this consumer belongs. By setting the same group id
  * multiple processes indicate that they are all part of the same consumer
  * group. This id is used to track the consumer cursors.
  *
  * "enable.auto.commit": (default true) If true, periodically commit the
  * offset of messages already fetched by the consumer, to Streams server.
  *
  * "auto.commit.interval.ms": (default 1000) The frequency in ms that the
  * consumer offsets are committed to streams server.
  *
  * "max.partition.fetch.bytes": (default 1024 * 1024) The number of bytes of
  * messages to attempt to fetch for each topic-partition in each fetch request.
  * These bytes will be read into memory for each partition, so this helps
  * control the memory used by the consumer.
  *
  * "fetch.min.bytes": (default 1) The minimum amount of data the server should
  * return for a fetch request. If insufficient data is available the request
  * will wait for that much data to accumulate before answering the request.
  *
  * "auto.offset.reset": (default largest) What to do when there is no initial
  * offset in Streams or if an offset is out of range:
  * earliest : automatically reset the offset to the earliest offset
  * latest : automatically reset the offset to the latest offset
  * anything else: throw an error to the consumer.
  *
  * "streams.consumer.default.stream": (default null)  Specifies the
  * default stream streams consumer will use when
  * streams_consumer_list_topics() is called with a null stream name.
  *
  */
STREAMS_API int32_t
streams_config_set(const streams_config_t config,
                   const char *name,
                   const char *value);

/** \brief Delete the config parameter.
  * \param config [IN] : streams config to delete.
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */
STREAMS_API int32_t
streams_config_destroy(streams_config_t config);

/** \brief Print the details of the configuration.
 *  \param config [IN] : streams config to print.
 *
 *  \return 0 on success. EINVAL on invalid inputs.
 */
STREAMS_API int32_t
streams_config_print(const streams_config_t config);

/**
 *\brief Inspect the fullPath and confirm if this is valid marlin path (</path>:<topic_name>)
 *\return 1 if path is valid and 0 if path is not.
 */
STREAMS_API bool
streams_is_full_path_valid(const char *fullPath, bool *isRegex);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif // STREAMS_COMMON_H__
