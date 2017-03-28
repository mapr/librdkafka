/* Copyright (c) 2015 & onwards. MapR Tech, Inc., All rights reserved */

#ifndef STREAMS_CONSUMERAPI_H__
#define STREAMS_CONSUMERAPI_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Create a new consumer client.
  * \param config [IN] : streams_config created by setting relevant
  *                      configuration key-value pairs for the consumer.
  * \param consumer [OUT] : pointer to the newly created consumer
  *
  * \return 0 on success, E_INVAL on bad input parameters.
  *
  * config, and rebalance callbacks can be NULL.
  */
STREAMS_API int32_t
streams_consumer_create(const streams_config_t config,
                        streams_consumer_t *consumer);


/** \brief Destroy consumer client.
  * \param consumer [IN] : client to be destroyed.
  *
  * \return 0 on success.
  *
  * This call waits for all outstanding OPs on this consumer to finish
  * before returning and can be a blocking call.
  */
STREAMS_API int32_t
streams_consumer_destroy(streams_consumer_t consumer);


/** \brief Subscribe to given topic partitions. Subscriptions
  * are not incremental. This list will replace the current assignment
  * (if there is one).
  * \param consumer [IN] : consumer client
  * \param tps [IN] : list of topic partitions to be subscribed.
  * \param tps_size [IN] : Size of the topic partition array
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This topic partition will participate in the next poll cycle. The
  * start offset for the subscription is set depending on the auto
  * offset reset policy. Subscriptions made through this API do not use the
  * consumer's group management functionality. As such, there will be no
  * rebalance operation triggered when group membership or cluster and topic
  * metadata change.
  */
STREAMS_API int32_t
streams_consumer_assign_partitions(const streams_consumer_t consumer,
                                   const streams_topic_partition_t *tps,
                                   uint32_t tps_size);

/** \brief Subscribe to given topics. Subscriptions
  * are not incremental. This list will replace the current
  * subscription (if there is one).
  * \param consumer [IN] : consumer client
  * \param topics [IN] : list of topic full paths.
  * \param topics_size [IN] : Size of the topic array
  * \param assign_cb [IN] : callback called when a new topic partition is
  *                         assigned to this listener
  * \param revoke_cb [IN] : callback called when a topic partition is revoked
  *                         from this listener's subscription list.
  * \param cb_ctx [IN] : context passed to assign_cb or revoke_cb
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This consumer will be subscribed to all the existing partitions of the given
  * topics if it is not part of a consumer group. As more partitions get added
  * to the these topics, those partitions will automatically get added to the
  * subscription list. If this consumer is part of a consumer group then all the
  * group management functionality will kick in and rebalancing operations will
  * be performed when group membership or cluster or topic metadata changes.
  */
STREAMS_API int32_t
streams_consumer_subscribe_topics(const streams_consumer_t consumer,
                                  const char **topics,
                                  uint32_t topics_size,
                                  const streams_rebalance_cb assign_cb,
                                  const streams_rebalance_cb revoke_cb,
                                  void *cb_ctx);

/** \brief Subscribe to topics that match this regex. Subscriptions
  * are not incremental. This list will replace the current regex
  * subscription (if there is one).
  * \param consumer [IN] : consumer client
  * \param regex [IN] : regex pattern to subscribe
  * \param topic_blacklist [IN] : regex pattern to ignore
  * \param assign_cb [IN] : callback called when a new topic partition is
  *                         assigned to this listener
  * \param revoke_cb [IN] : callback called when a topic partition is revoked
  *                         from this listener's subscription list.
  * \param cb_ctx [IN] : context passed to assign_cb or revoke_cb
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This consumer will be subscribed to topics that match the
  * regex. Just like topic subscription, as more partitions get added
  * to the these topics, those partitions will automatically get added to the
  * subscription list. If this consumer is part of a consumer group then all the
  * group management functionality will kick in and rebalancing operations will
  * be performed when group membership or cluster or topic metadata
  * changes.
  *
  * When new topics that match this regex is created, this consumer
  * will subscribe to them (with some time delay, which depends on the
  * metadata refresh configuration parameter).
  */
STREAMS_API int32_t
streams_consumer_subscribe_regex(const streams_consumer_t consumer,
                                 const char *regex,
                                 const char *topic_blacklist,
                                 const streams_rebalance_cb assign_cb,
                                 const streams_rebalance_cb revoke_cb,
                                 void *cb_ctx);

/** \brief Unsubscribe from topics and partitions currently subscribed to.
  * \param consumer [IN] : consumer client
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * All subscription will be immediately removed from the subscription list
  * and should not participate in the next poll cycle.
  */
STREAMS_API int32_t
streams_consumer_unsubscribe(const streams_consumer_t consumer);

/** \brief Get a list of topic partitions currently assigned to this consumer
  * \param consumer [IN] : consumer client
  * \param tps [OUT] : array of topic partitions
  * \param tps_size [OUT] : Size of the topic partition array
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This returns a list of topic partitions that have been currently assigned to
  * this consumer. Either because the subscriptions were made using topic
  * partition subscription API or because these topic partitions from a
  * subscribed topic have been assigned to this consumer.
  * The topic partitions returned as part of this API should be explicitly
  * freed by the user.
  */
STREAMS_API int32_t
streams_consumer_get_assignment(const streams_consumer_t consumer,
                                streams_topic_partition_t **tps,
                                uint32_t *tps_size);

/** \brief Get a list of topics user has subscribed to
  * \param consumer [IN] : consumer client
  * \param topic_names [OUT] : array of topic names
  * \param topic_sz [OUT] : Size of the topic name array
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * This returns a list of topics that have been currently subscribed
  * by this consumer.
  * The topic names returned as part of this API should be explicitly freed
  * by the user.
  */
STREAMS_API int32_t
streams_consumer_get_subscription(const streams_consumer_t consumer,
                                  char ***topic_names,
                                  uint32_t *topic_size);

/** \brief Poll to get next set of messages on subscribed partitions
  * \param consumer [IN] : consumer client
  * \param timeout [IN] : time to wait before returning in case there are not
  *                       configured min message bytes to return.
  * \param records [OUT] : array of consumer record. For every topic partition
  *                        that has messages there is one consumer record
  *                        returned.
  * \param records_size [OUT] : Size of the records array
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Fetches data for the topics or partitions specified using one of the
  * subscribe APIs. It is an error to not have subscribed to any topics or
  * partitions before polling for data. The offset used for fetching the data
  * is governed by whether or not seek API is used. If seek is used, it will
  * use the specified offsets on startup and on every rebalance, to consume
  * data from that offset sequentially on every poll. If not, it will use the
  * last checkpointed offset using commit for the subscribed list of partitions.
  */

STREAMS_API int32_t
streams_consumer_poll(const streams_consumer_t consumer,
                      uint64_t timeout,
                      streams_consumer_record_t **records,
                      uint32_t *records_size);


/** \brief Commit the given offsets for given topic partitions synchronously
  * \param consumer [IN] : consumer client
  * \param tps [IN] : topic partition array
  * \param cursors [IN] : cursors corresponding to each topic partition
  * \param tps_size [IN] : size of the topic partition array
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Commit the given offset to the streams server. If the consumer is not part
  * of a consumer group then we do not perform any on-disk operation but only
  * remember the cursors for these partition in memory.
  */

STREAMS_API int32_t
streams_consumer_commit_sync(const streams_consumer_t consumer,
                             const streams_topic_partition_t *tps,
                             const int64_t *cursors,
                             uint32_t tps_size);

/** \brief Commit the given offsets for given topic partitions asynchronously
  * \param consumer [IN] : consumer client
  * \param tps [IN] : topic partition array
  * \param cursors [IN] : cursors corresponding to each topic partition
  * \param tps_size [IN] : size of the topic partition array
  * \param commit_cb [IN] : callback called when commit completes
  * \param cb_ctx [IN] : context passed to commit_cb. Can be NULL
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Commit the given offset to the streams server. If the consumer is not part
  * of a consumer group then we do not perform any on-disk operation but only
  * remember the cursors for these partition in memory.
  *
  * Any buffer attached to the tps, cursors must not be altered until
  * the callback has been received.
  *
  * Any buffer attached to the callback should be deleted by the user.
  */
STREAMS_API int32_t
streams_consumer_commit_async(const streams_consumer_t consumer,
                              const streams_topic_partition_t *tps,
                              const int64_t *cursors,
                              uint32_t tps_size,
                              const streams_commit_cb commit_cb,
                              void *cb_ctx);

/** \brief Commit the last read offset for all topic partitions synchronously
  * \param consumer [IN] : consumer client
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Commit the offset of the message last read by this consumer to the streams
  * server. If the consumer is not part of a consumer group then we do not
  * perform any on-disk operation but only remember the cursors for these
  * partition in memory. Using a group.id parameter is recommended if you want
  * the cursors to persist across consumer restarts.
  *
  * All memory attached to tps and cursors should not be freed until
  * the call returns.
  */
STREAMS_API int32_t
streams_consumer_commit_all_sync(const streams_consumer_t consumer);

/** \brief Commit the last read offset for all topic partitions asynchronously
  * \param consumer [IN] : consumer client
  * \param commit_cb [IN] : callback called when commit completes.
  *                         Can be NULL
  * \param cb_ctx [IN] : context passed to commit_cb. Can be NULL
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Commit the offset of the message last read by this consumer to the streams
  * server. If the consumer is not part of a consumer group then we do not
  * perform any on-disk operation but only remember the cursors for these
  * partition in memory. Using a group.id parameter is recommended if you want
  * the cursors to persist across consumer restarts.
  *
  * If the commit callback is provided, commit callback will be called
  * and the memory for the parameters passed into the commit callback
  * should be freed by the application.
  */
STREAMS_API int32_t
streams_consumer_commit_all_async(const streams_consumer_t consumer,
                                  const streams_commit_cb commit_cb,
                                  void *cb_ctx);

/** \brief Seek to the given offset
  * \param consumer [IN] : consumer client
  * \param partition [IN] : topic partition
  * \param offset [IN] : seek offset
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Seek to this offset in memory. The next poll will start fetching data from
  * this offset.
  */
STREAMS_API int32_t
streams_consumer_seek(const streams_consumer_t consumer,
                      const streams_topic_partition_t partition,
                      int64_t offset);

/** \brief Seek to the beginning of the partition for the given partitions
  * \param consumer [IN] : consumer client
  * \param tps [IN] : topic partition array
  * \param tps_size [IN] : topic partition array size
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Seek to the very first message for all the given partitions. The next poll
  * will start fetching data from this offset.
  */
STREAMS_API int32_t
streams_consumer_seek_to_beginning(const streams_consumer_t consumer,
                                   const streams_topic_partition_t *tps,
                                   uint32_t tps_size);

/** \brief Seek to the end of the partition for all partitions
  * \param consumer [IN] : consumer client
  * \param tps [IN] : topic partition array
  * \param tps_size [IN] : topic partition array size
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Seek to the end for all the given partitions. The next poll will
  * start fetching data from this offset.
  */
STREAMS_API int32_t
streams_consumer_seek_to_end(const streams_consumer_t consumer,
                             const streams_topic_partition_t *tps,
                             uint32_t tps_size);


/** \brief Query the committed offset for this topic partition by this consumer
  * \param consumer [IN] : consumer client
  * \param tp [IN] : topic partition
  * \param offset [OUT] : committed offset
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Committed offset for this consumer group or by this consumer in case this
  * consumer is not part of any group.
  */
STREAMS_API int32_t
streams_consumer_committed(const streams_consumer_t consumer,
                           const streams_topic_partition_t tp,
                           int64_t *offset);

/** \brief Query the next read offset for this topic partition for this consumer
  * \param consumer [IN] : consumer client
  * \param tp [IN] : topic partition
  * \param offset [OUT] : next read offset
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Minimum offset of the next message that will be returned on poll.
  */
STREAMS_API int32_t
streams_consumer_position(const streams_consumer_t consumer,
                          const streams_topic_partition_t tp,
                          int64_t *offset);

/** \brief Get number of partitions for a topic
  * \param consumer [IN] : consumer handle
  * \param topic_name [IN] : topic name
  * \param num_partitions [OUT] : number of partitions for the topic
  *
  * \return 0 on success. Error code on failure.
  *
  * Returns the number of partitions for the topic.  This function may
  * make a rpc to the server to fetch the topic metadata, unless the
  * client already has it cached.
  */

STREAMS_API int32_t
streams_consumer_get_num_partitions(const streams_consumer_t consumer,
                                    const char *topic_name,
                                    uint32_t *num_partitions);

/** \brief Get topic information for all topics in the stream
  * \param consumer [IN] : consumer handle
  * \param stream_name [IN] : stream name (can be NULL or regex)
  * \param topic_names [OUT] : an array of topic names for the stream
  * \param num_partitions [OUT] : number of partitions for each of the
  *                               topic in the stream
  * \param num_topics [OUT] : size of the two arrays, topic_names and
  *                           num_partitions
  *
  * \return 0 on success. Error code on failure.
  *
  * Returns an array of topics for the provided stream.  If
  * stream_name is set to NULL, then the list of topics from the
  * default consumer stream will be fetched.  If default consumer
  * stream is not set in the configuration, returns an error.  The
  * caller is responsible for freeing the arrays returned.
  */

STREAMS_API int32_t
streams_consumer_list_topics(const streams_consumer_t consumer,
                             const char *stream_name,
                             char ***topic_names,
                             int32_t **num_partitions,
                             uint32_t *num_topics);

/** \brief Suspend fetching records from the provided topic partitions
 * \param consumer [IN] : consumer handle
 * \param tps [IN] : array of topic partitions
 * \param tps_size [IN] : array size
 *
 * \return 0 on success.  Error code on faliure.
 *
 * Pauses fetching records from the provided topic partitions.  This
 * does not trigger consumer group's partition rebalance.
 */
STREAMS_API int32_t
streams_consumer_pause(const streams_consumer_t consumer,
                       const streams_topic_partition_t *tps,
                       uint32_t tps_size);

/** \brief Resume fetching records from the provided topic partitions
 * \param consumer [IN] : consumer handle
 * \param tps [IN] : array of topic partitions
 * \param tps_size [IN] : array size
 *
 * \return 0 on success.  Error code on faliure.
 *
 * Resumes fetching records from the provided topic partitions.  This
 * does not trigger consumer group's partition rebalance.
 */
STREAMS_API int32_t
streams_consumer_resume(const streams_consumer_t consumer,
                        const streams_topic_partition_t *tps,
                        uint32_t tps_size);

/** \brief Wake up the consumer.
 * \param consumer [IN] : consumer handle
 *
 * \return 0 on success.  Error code on faliure.
 *
 * The thread which is blocking in an operation will return with an
 * error.  This method is useful in particular to abort a long poll.
 */
STREAMS_API int32_t
streams_consumer_wakeup(const streams_consumer_t consumer);

/** \brief Get the topic partition for this consumer record
  * \param consumer [IN] : consumer record
  * \param tp [OUT] : topic partition
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Topic partition for this consumer record. Consumer record itself has a topic
  * partition and a list of messages for that topic partition.
  */
STREAMS_API int32_t
streams_consumer_record_get_topic_partition(const streams_consumer_record_t cr,
                                            streams_topic_partition_t *tp);

/** \brief Count of messages in this consumer record
  * \param consumer [IN] : consumer record
  * \param num_msgs [OUT] : number of messages
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Number of messages in this consumer record. Consumer record itself has a
  * topic partition and a list of messages for that topic partition.
  */
STREAMS_API int32_t
streams_consumer_record_get_message_count(const streams_consumer_record_t cr,
                                          uint32_t *num_msgs);

/** \brief Return the error in this consumer record
  * \param consumer [IN] : consumer record
  * \param error [OUT] : topic partition error
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Return the error on topic partition.
  */
STREAMS_API int32_t
streams_consumer_record_get_error(const streams_consumer_record_t cr,
                                  int *error);

/** \brief Destroy the consumer record
  * \param consumer [IN] : consumer record
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Destroys this consumer record. All consumer records returned through a poll
  * API need to be freed before the memory is actually freed.
  */
STREAMS_API int32_t
streams_consumer_record_destroy(streams_consumer_record_t cr);


/** \brief Get the key for the i'th message in the consumer record
  * \param consumer [IN] : consumer record
  * \param msg_index [IN] : index of the message
  * \param key [OUT] : byte array for the key. Can be NULL
  * \param key_size [OUT] : byte array size
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Extract the key from the i'th message in the consumer record.
  */
STREAMS_API int32_t
streams_msg_get_key(const streams_consumer_record_t cr,
                    uint32_t msg_index,
                    void **key,
                    uint32_t* key_size);

/** \brief Get the value for the i'th message in the consumer record
  * \param consumer [IN] : consumer record
  * \param msg_index [IN] : index of the message
  * \param value [OUT] : byte array for the value
  * \param value_size [OUT] : byte array size
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Extract the value from the i'th message in the consumer record.
  */
STREAMS_API int32_t
streams_msg_get_value(const streams_consumer_record_t cr,
                      uint32_t msg_index,
                      void **value,
                      uint32_t* value_size);

/** \brief Get the producer for the i'th message in the consumer record
  * \param consumer [IN] : consumer record
  * \param msg_index [IN] : index of the message
  * \param producer [OUT] : char pointer for the producer
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Extract the producer from the i'th message in the consumer record.
  */
STREAMS_API int32_t
streams_msg_get_producer(const streams_consumer_record_t cr,
                      uint32_t msg_index,
                      char **producer);

/** \brief Get the offset for the i'th message in the consumer record
  * \param consumer [IN] : consumer record
  * \param msg_index [IN] : index of the message
  * \param offset [OUT] : offset of the message
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Extract the offset from the i'th message in the consumer record.
  */
STREAMS_API int32_t
streams_msg_get_offset(const streams_consumer_record_t cr,
                       uint32_t msg_index,
                       int64_t *offset);

/** \brief Get the timestamp for the i'th message in the consumer record
  * \param consumer [IN] : consumer record
  * \param msg_index [IN] : index of the message
  * \param timestamp [OUT] : timestamp of the message
  *
  * \return 0 on success. EINVAL on invalid inputs.
  *
  * Extract the timestamp from the i'th message in the consumer record.
  */
STREAMS_API int32_t
streams_msg_get_timestamp(const streams_consumer_record_t cr,
                          uint32_t msg_index,
                          int64_t *timestamp);

/** \brief Get Consumer groups
  * \param consumer [IN] : consumer handle
  * \param group_name [IN] : group name whose info to be returned
  *                          NULL for all the groups)
  * \param timeout_ms [IN] : timeout in milliseconds. Must be > 0
  * \param num_groups [OUT] : number of groups
  * \param assignmentSize [OUT] : assignment per group per stream per topic
  * \param assignVector [OUT] : vector <AssignInfo *>
  * \return 0 on success. Error code on failure.
  *
  */
STREAMS_API int32_t
streams_consumer_list_groups(const streams_consumer_t consumer,
                             const char *stream_name,
                             const char *group_name,
                             int timeout_ms,
                             uint32_t *num_groups,
                             uint32_t *assignmentSize,
                             streams_assign_list_t *assignVector);

/** \brief Get Stream name from AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param stream [OUT] : stream
  * \param stream_size [OUT] : stream len
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_get_stream(streams_assign_list_t assignInfoVec, int index,
                               const char **stream,
                               uint32_t* stream_size);

/** \brief Get topic name from AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param topic [OUT] : stream
  * \param topic_size [OUT] : topic len
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
  streams_assign_info_get_topic ( streams_assign_list_t assignInfoVec, int index,
                               const char **topic,
                               uint32_t* topic_size);

/** \brief Get group name from AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param group [OUT] : group name
  * \param group_size [OUT] : group len
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_get_groupid(streams_assign_list_t assignInfoVec, int index,
                               const char **group,
                               uint32_t* group_size);

/** \brief Get error per group per topic from AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param err [OUT] : Error per topic per group
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_get_error(streams_assign_list_t assignInfoVec, int index,
                                      int *error);

/** \brief Get #of consumers per group per topic from AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param num_consumers [OUT] : #of consumers per topic per group
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_get_num_consumers(streams_assign_list_t assignInfoVec, int index,
                                      int *num_consumers);

/** \brief Get consumers from AssignInfo
  * \param group [OUT] : group name
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \parat consumers [OUT] : array of consumers names
  * \param num_consumers [OUT] : size of 'consumers'
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_list_consumers(streams_assign_list_t assignInfoVec, int index,
                                   char ***consumers,
                                   int *num_consumers);

/** \brief Get assignments for all consumers in AssignInfo
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \param assignments [OUT] : partition assignments array
  * \param param size [OUT] : 'assignments' size
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_get_assignment(streams_assign_list_t assignInfoVec, int index,
                                   int consumerId,
                                   int **assignments,
                                   int *size);

/** \brief delete from AssignVector
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_destroy_all(streams_assign_list_t assignInfoVec);

/** \brief Delete AssignInfo from assignInfoVec
  * \param assignInfoVec [IN] : assignVector returned from
  *                      streams_consumer_list_groups api
  * \param index [IN] : index
  * \return 0 on success. -1 on failure.
  */
STREAMS_API int32_t
streams_assign_info_destroy(streams_assign_list_t assignInfoVec, int index);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif // STREAMS_CONSUMERAPI_H__
