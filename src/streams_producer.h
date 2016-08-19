/* Copyright (c) 2015 & onwards. MapR Tech, Inc., All rights reserved */

#ifndef STREAMS_PRODUCERAPI_H__
#define STREAMS_PRODUCERAPI_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Create a producer client.
  * \param config [IN] : streams_config created by setting relevant
  *                      configuration key-value pairs for the
  *                      producer. This can be NULL.
  * \param producer [OUT] : producer handle
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  */
STREAMS_API int32_t
streams_producer_create(const streams_config_t config,
                        streams_producer_t *producer);

/** \brief Close the producer client.
  * \param producer [IN] : producer handle
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This call waits for all the internal ops to complete and can be a
  * blocking call.
  */
STREAMS_API int32_t
streams_producer_destroy(streams_producer_t producer);


/** \brief Send a message to the producer
  * \param producer [IN] : producer handle
  * \param record [IN] : record to send
  * \param cb [IN] : completion callback
  * \param cb_ctx [IN] : completion callback context. Can be NULL.
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Asynchronously send a record to a topic and invoke the provided callback
  * when the send has been acknowledged. The result of the send specifies  the
  * partition the record was sent to and the offset it was assigned.
  *
  * Any buffer attached to the the record object must not be altered
  * until the callback has been received.  Sending the same record
  * before the callback is made will return an error.
  */

STREAMS_API int32_t
streams_producer_send(const streams_producer_t producer,
                      const streams_producer_record_t record,
                      const streams_producer_cb cb,
                      void *cb_ctx);

/** \brief Flush all outstanding records for this producer immediately..
  * \param producer [IN] : producer handle
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Synchronous flush call that flushes all the records currently
  * buffered on the producer to streams server.
  */

STREAMS_API int32_t
streams_producer_flush(const streams_producer_t producer);

/** \brief Get number of partitions for a topic
  * \param producer [IN] : producer handle
  * \param num_partitions [OUT] : number of partitions for the topic
  *
  * \return 0 on success.  Error code on invalid inputs or errors.
  *
  * Returns the number of partitions for the topic.  This function may
  * make a rpc to the server to fetch the topic metadata, unless the
  * client already has it cached.
  */

STREAMS_API int32_t
streams_producer_get_num_partitions(const streams_producer_t producer,
                                    const char *topic_name,
                                    int *num_partitions);

/** \brief Create a producer record for send
  * \param tp [IN] : topic partition for the record
  * \param key [IN] : message key. Can be NULL
  * \param key_size [IN] : size of key byte array
  * \param value [IN] : message value
  * \param value_size [IN] : size of value byte array
  * \param pr [OUT] : a producer record created using above parameters
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Creates a producer record to be sent to the producer. The topic
  * partition, key and value can not be freed till the callback for send
  * has been called. If a partition is specified in the topic
  * partition then the record is sent to that partition, if no
  * partition is specified but the message has a key then a partition is
  * chosen based by hashing the key. If both key and partition id are missing
  * then records are evenly distributed across all the available
  * partitions of this topic.
  */

STREAMS_API int32_t
streams_producer_record_create(streams_topic_partition_t tp,
                               const void *key,
                               uint32_t key_size,
                               const void *value,
                               uint32_t value_size,
                               streams_producer_record_t *pr);

/** \brief Returns key and its size attached to this record.
  * \param pr [IN] : a producer record
  * \param key [OUT] : key attached to the record
  * \param key_size [OUT] : size of the key returned
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */

STREAMS_API int32_t
streams_producer_record_get_key(const streams_producer_record_t pr,
                                const void **key,
                                uint32_t *key_size);

/** \brief Returns value and its size attached to this record.
  * \param pr [IN] : a producer record
  * \param value [OUT] : value attached to the record
  * \param value_size [OUT] : size of the value returned
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */

STREAMS_API int32_t
streams_producer_record_get_value(const streams_producer_record_t pr,
                                  const void **value,
                                  uint32_t *value_size);

/** \brief Returns streams_topic_partition_t attached to this record.
  * \param pr [IN] : a producer record
  * \param tp [OUT] : a topic partition attached to the producer record
  *
  * \return 0 on success. EINVAL on invalid inputs.
  */

STREAMS_API int32_t
streams_producer_record_get_topic_partition(const streams_producer_record_t pr,
                                            streams_topic_partition_t *tp);

/** \brief Destroy the given producer record
  * \param pr [IN] : producer record to be deleted.
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This does not destroy the topic partition and key/value used to create
  * the producer record. Those need to be freed separately.
  */

STREAMS_API int32_t
streams_producer_record_destroy(streams_producer_record_t pr);


#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif // STREAMS_PRODUCERAPI_H__
