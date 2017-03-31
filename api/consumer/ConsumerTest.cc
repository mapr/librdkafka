#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include "Consumer.h"
static int assignedCount = 0;
static int revokedCount = 0;
int ConsumerTest::runConsumerCreateTest(bool isConfValid,
                                          bool isTypeValid) {
  rd_kafka_conf_t *conf;
  if(isConfValid)
    conf = rd_kafka_conf_new();
  else
    conf = NULL;

  rd_kafka_type_t type;
  char errstr[512];
  int retVal = SUCCESS;
  if(isTypeValid)
    type = RD_KAFKA_CONSUMER;
  else
    type = (rd_kafka_type_t) NULL;

  try {
    rd_kafka_t *consumer = rd_kafka_new(type, conf, errstr, sizeof(errstr));
    if (!consumer) {
      cerr << "\nConsumerCreateTest: Failed to create new consumer\n" <<  errstr;
      return CONSUMER_CREATE_FAILED;
    }
    rd_kafka_consumer_close(consumer);
    rd_kafka_destroy(consumer);
    return retVal;
  } catch (const std::exception& e) {
    cerr << "\nConsumerCreateTest: Exception occured," << e.what() << "\n";
    return INVALID_ARGUMENT;
  }
}

void populateTopicPartitionList (char *strName, int numStreams, int numTopics,
                                int numPartitions, int topicType, bool isAssign,
                                char *cDefaultStr,
                                unordered_map <string, int, keyHasher> *subscrMap,
                                rd_kafka_topic_partition_list_t **outList) {
  for (int s = 0; s <  numStreams; ++s) {
    for (int t = 0; t < numTopics; ++t) {
      char currentName[100];
      memset ( currentName, '\0', 100);
      int tempIndex = s* numTopics + t;
      if (cDefaultStr) {
        snprintf(currentName, sizeof(currentName), "topic%d", t);
      } else {
        switch (topicType)  {
        case 0: snprintf(currentName, sizeof(currentName),
                    "%s%d:topic%d",  strName, s, t);
                break;
        case 1: snprintf(currentName, sizeof(currentName),
                    "%s%dtopic%d",  strName, s, t);
                break;
        case 2: //50-50 Mapr and Kafka topics
                if( tempIndex % 2 == 0)
                  snprintf(currentName, sizeof(currentName),
                      "%s%d:topic%d",  strName, s, t);
                else
                  snprintf(currentName, sizeof(currentName),
                      "%s%dtopic%d",  strName, s, t);
                break;
        }
      }
      char temp[200];
      if(isAssign) {
        for(int p = 0; p < numPartitions ; ++p ) {
          memset(temp, 0, 200);
          rd_kafka_topic_partition_list_add(*outList, currentName , p);
          if(cDefaultStr)
            snprintf(temp, sizeof(temp),
                "%s0:%s:%d",cDefaultStr, currentName, p);
          else
            snprintf(temp, sizeof(temp), "%s:%d", currentName, p);
          if (subscrMap) (*subscrMap)[temp] = 1;
        }
      } else {
          if(cDefaultStr) {
            memset(temp, 0, 200);
            snprintf(temp, sizeof(temp), "%s0:%s",cDefaultStr, currentName);
            if (subscrMap) (*subscrMap)[temp] = 1;
          } else {
            if (subscrMap) (*subscrMap)[currentName] = 1;
          }

          rd_kafka_topic_partition_list_add(*outList, currentName,
                                            RD_KAFKA_PARTITION_UA);
      }
    }
  }
}

bool verifySubscriptions (rd_kafka_topic_partition_list_t *inList,
                          unordered_map <string, int, keyHasher> subscrMap,
                          bool isAssign) {
  int count =0;
  for (int i = 0; i < inList->cnt; i++) {
    if (isAssign) {
      char temp[200];
      memset(temp, '\0', 200);
      snprintf(temp, sizeof(temp), "%s:%d",
          inList->elems[i].topic, inList->elems[i].partition);
      if (subscrMap.count (temp) > 0)
          count ++;
    } else {
        if (subscrMap.count (inList->elems[i].topic) > 0)
          count ++;
    }
  }

  if (count == inList->cnt)
    return true;

  return false;
}

void subscribe_consumer (char *strName, rd_kafka_t *consumer,
                         unordered_map <string, int, keyHasher> *subscrMap,
                         bool kafkaConsumer) {
  rd_kafka_topic_partition_list_t *init_list =
                                         rd_kafka_topic_partition_list_new(1);
  if (kafkaConsumer)
    populateTopicPartitionList(strName, 1, 1, 1, 1, false, NULL, subscrMap, &init_list);
  else
    populateTopicPartitionList(strName, 1, 1, 1, 0, false, NULL, subscrMap, &init_list);

  rd_kafka_subscribe(consumer, init_list);
  rd_kafka_topic_partition_list_destroy (init_list);
}

void rebalance_cb(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                           rd_kafka_topic_partition_list_t *partitions,
                           void *opaque) {
  switch (err)
  {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
        atomic_add32 (&assignedCount, 1);
        break;
    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
        atomic_add32 (&revokedCount, 1);
        break;
    default:
        break;
  }
}
/**
 * consumerType: 0 (undetermined type - not subscribed to any topic)
 *               1 (Mapr consumer - subscribed to mapr topic)
 *               2 (Kafka consumer - subscribed to kafka topic)
 */
int ConsumerTest::runSubscribeTest (char *strName, int numStreams, int numTopics,
                                    int numPartitions, bool isConsumerValid,
                                    int consumerType, int topicType,
                                    const char *groupId, bool isAssign,
                                    char *cDefaultStr) {
    rd_kafka_t *consumer;
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    char errstr[128];
    if (cDefaultStr) {
    char cStr[strlen(cDefaultStr) +1];
    memset (cStr, 0, strlen(cDefaultStr) +1);
    snprintf (cStr, sizeof(cStr)+1, "%s0", cDefaultStr);
    rd_kafka_conf_set(conf, "streams.consumer.default.stream", cStr,
                      errstr, sizeof(errstr));
    }
    if (groupId)
      rd_kafka_conf_set(conf, "group.id", groupId, errstr, sizeof(errstr));
    rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);
    if (isConsumerValid)
      consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    else
      consumer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    int nTotalTp = numStreams * numTopics * numPartitions;
    std::unordered_map<std::string, int, keyHasher> subscrMap;
    rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(nTotalTp);
    int retVal = SUCCESS;
    if (consumerType == 1)// mapr consumer
      subscribe_consumer (strName, consumer, &subscrMap, false);
    else if (consumerType == 2)// kafka consumer
      subscribe_consumer (strName, consumer, &subscrMap, true);
    else// default consumer - not mapr/kafka
      ;

    switch (topicType) {
    case 0: // All Mapr topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 0, isAssign, cDefaultStr, &subscrMap,
                                       &tp_list);
            break;

    case 1: // All kafka topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 1, isAssign, cDefaultStr, &subscrMap,
                                       &tp_list);
            break;

    case 2: // Mixed topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 2, isAssign, cDefaultStr, &subscrMap,
                                       &tp_list);
            break;
    default:
            retVal = INVALID_ARGUMENT;
            break;
    }
    rd_kafka_topic_partition_list_t *outList = NULL;
    if (isAssign) {
      retVal = rd_kafka_assign(consumer, tp_list);
      rd_kafka_assignment(consumer, &outList);
    } else {
      retVal = rd_kafka_subscribe(consumer, tp_list);
      rd_kafka_subscription(consumer, &outList);
    }
    if (retVal == SUCCESS) {
      if (!verifySubscriptions (outList, subscrMap, isAssign)){
        cerr << "\n Subscription mismatch. Verification failed!";
        retVal = -1;
      }
    }
    if(tp_list)
      rd_kafka_topic_partition_list_destroy(tp_list);
    if(outList)
      rd_kafka_topic_partition_list_destroy(outList);
    rd_kafka_consumer_close(consumer);
    rd_kafka_destroy (consumer);
    return retVal;
}

uint64_t ConsumerTest::runPollTest(char *path, int nstreams, int ntopics,int nparts,
                              int nmsgs, int msgsize, int flag,
                              bool roundRb, int nslowtopics, bool print,
                              uint64_t timeout, const char* groupid, bool topicSub,
                              bool autoCommit, bool verify) {
  Producer p;
  uint64_t cb = 0;
  p.runTest (path, nstreams, ntopics, nparts, nmsgs,
                             msgsize, flag, roundRb, nslowtopics, false, timeout, &cb);
  sleep (2);
  Consumer c;
  return c.runTest (path, nstreams, ntopics, nparts, nmsgs, groupid, topicSub,
                            autoCommit, verify, print);
}

int ConsumerTest::runUnsubscribeTest (char *path, int nstreams, int ntopics,
                                       int nparts, int nmsgs,
                                       int msgsize, bool roundRb, int nslowtopics,
                                       bool print, uint64_t timeout,
                                       const char* groupid) {
  //Produce data to said topic partitions on a stream
  Producer p;
  uint64_t cb = 0;
  p.runTest (path, nstreams, ntopics, nparts, nmsgs, msgsize, RD_KAFKA_MSG_F_COPY,
                      roundRb, nslowtopics, false, timeout, &cb);
  //Create consumer
  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  if (groupid)
        rd_kafka_conf_set(conf, "group.id", groupid, errstr, sizeof(errstr));

  rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);
  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf, topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  std::unordered_map<std::basic_string<char>, int, keyHasher> subscrMap;
  //Subscribe to topics
  int nTotalTp = nstreams * ntopics * nparts;
  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(nTotalTp);
  populateTopicPartitionList(path, nstreams, ntopics,
                                       nparts, 0, false, NULL,  &subscrMap, &tp_list);
  rd_kafka_subscribe (consumer, tp_list);
  int totalMsgs = nstreams * ntopics * nparts * nmsgs;
  //start consuming
  int consumedCount = 0;
  while (true) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 1000);
    if (rkmessage) {
      consumedCount++;
      if (consumedCount == totalMsgs/2) {
        //half way through, unsubscribe from topic
        rd_kafka_unsubscribe(consumer);
        rd_kafka_message_destroy (rkmessage);
        break;
      }
      rd_kafka_message_destroy (rkmessage);
    }
  }
  sleep (3);
  int j = 5;
  while (j > 0) {
    rd_kafka_message_t *rkmessage2;
    //try consuming again after unsubscribe
    rkmessage2 = rd_kafka_consumer_poll (consumer, 1000);
    if (rkmessage2) {
      consumedCount++;
      rd_kafka_message_destroy (rkmessage2);
    }
    j--;
  }
  rd_kafka_topic_partition_list_destroy (tp_list);
  rd_kafka_consumer_close (consumer);
  rd_kafka_destroy (consumer);
  return consumedCount;
}

void add_rd_kafka_topic_partition_list(rd_kafka_topic_partition_list_t **inList,
                                      rd_kafka_topic_partition_list_t *addList){
  for (int i =0; i < addList->cnt; i++) {
    rd_kafka_topic_partition_t *tp = NULL;
    tp = rd_kafka_topic_partition_list_find (*inList, addList->elems[i].topic,
                                                   addList->elems[i].partition);
    if(tp == NULL)
      return;
    rd_kafka_topic_partition_list_add (*inList, addList->elems[i].topic,
                                       addList->elems[i].partition);
  }
}

void delete_rd_kafka_topic_partition_list(rd_kafka_topic_partition_list_t **inList,
                                      rd_kafka_topic_partition_list_t *deleteList){
  for (int i =0; i < deleteList->cnt; i++) {
    rd_kafka_topic_partition_list_del (*inList, deleteList->elems[i].topic,
                                       deleteList->elems[i].partition);
  }
}

void assign_revoke_rebalance_cb_test(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                           rd_kafka_topic_partition_list_t *partitions,
                           void *opaque) {
  RebalanceCbCtx *ctx = (RebalanceCbCtx *) opaque;
  switch (err)
  {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
        add_rd_kafka_topic_partition_list (&(ctx->subscribedList) , partitions);
        ctx->subscriptionCnt++;
        break;
    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
        delete_rd_kafka_topic_partition_list (&(ctx->subscribedList) , partitions);
        ctx->subscriptionCnt--;
        break;
    default:
        break;
  }
}


void *createAndStartConsumer (void *thArg) {
  ConsumerThreadArgs *args = static_cast<ConsumerThreadArgs *> (thArg);
  char *strName = args->path;
  int minPartId = args->minPartId;
  int maxPartId = args->maxPartId;
  bool killSelf = args->killSelf;
  char *groupid = args->group;
  int id = args->id;
  int numMsgs = args->numMsgs;
  struct RebalanceCbCtx *ctx = args->ctx;

  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  rd_kafka_conf_set(conf, "group.id", groupid , errstr, sizeof(errstr));
  ctx->subscriptionCnt = 0;
  ctx->unsubscribeCnt = 0;
  ctx->id = id;
  ctx->subscribedList = rd_kafka_topic_partition_list_new(0);
  rd_kafka_conf_set_opaque(conf, ctx);
  rd_kafka_conf_set_rebalance_cb(conf, assign_revoke_rebalance_cb_test);

  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf, topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(1);
  int ntopics = 1;
  //populate tp list for consumer 1 and consumer 2
  for (int t = 0; t < ntopics; ++t) {
    char currentName[100];
    memset ( currentName, '\0', 100);
    snprintf(currentName, sizeof(currentName),
        "%s0:topic%d",  strName, args->topicIds[t]);
    for(int p = minPartId; p < maxPartId ; ++p ) {
        rd_kafka_topic_partition_list_add(tp_list, currentName, p);
    }
  }

  rd_kafka_subscribe (consumer, tp_list);
  int msgCount = 0;
  while (msgCount < numMsgs) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      if((msgCount == 500) && killSelf) {
        rd_kafka_unsubscribe (consumer);
        sleep (3);
        rd_kafka_message_destroy (rkmessage);
        break;
      }
      if(msgCount%100 == 0) {
        //Sleep after every 100 msg to increase parallelism between threads
        sleep (3);
      }
      rd_kafka_message_destroy (rkmessage);
    }
  }
  rd_kafka_topic_partition_list_destroy (tp_list);
  rd_kafka_consumer_close (consumer);
  rd_kafka_destroy (consumer);
  return (void *)args;
}

void ConsumerTest::runAssignRevokeCbTest(char * strName, int nparts) {
  Producer p;
  uint64_t cb = 0;
  nparts = 12;
  int nmsgs = 10 * 1000; // produce 10000 msgs on each partition.
  p.runTest (strName, 1, 1, nparts, nmsgs,
             200, RD_KAFKA_MSG_F_COPY, true, 0, false, 120, &cb);

  //create 2 consumers part of consumer group
  int topicSet1[2] = {0};
  int topicSet2[3] = {0};
  int topicSet3[2] = {0};
  int topicSet4[4] = {0};
  struct RebalanceCbCtx rb_ctx1, rb_ctx2, rb_ctx3, rb_ctx4;
  ConsumerThreadArgs threadArgs[4];
  threadArgs[0].path = strName;
  threadArgs[0].topicIds = topicSet1;
  threadArgs[0].minPartId = 0;
  threadArgs[0].maxPartId = nparts;
  threadArgs[0].killSelf = false;
  threadArgs[0].group = "AssignRevokeCbTest";
  threadArgs[0].id = 0;
  threadArgs[0].numMsgs = 9000;
  threadArgs[0].ctx = &rb_ctx1;

  threadArgs[1].path = strName;
  threadArgs[1].topicIds = topicSet2;
  threadArgs[1].minPartId = 0;
  threadArgs[1].maxPartId = nparts;
  threadArgs[1].killSelf = false;
  threadArgs[1].group = "AssignRevokeCbTest";
  threadArgs[1].id = 1;
  threadArgs[1].numMsgs = 9000;
  threadArgs[1].ctx = &rb_ctx2;

  threadArgs[2].path = strName;
  threadArgs[2].topicIds = topicSet3;
  threadArgs[2].minPartId = 0;
  threadArgs[2].maxPartId = nparts;
  threadArgs[2].killSelf = true;
  threadArgs[2].group = "AssignRevokeCbTest";
  threadArgs[2].id = 2;
  threadArgs[2].numMsgs = 9000;
  threadArgs[2].ctx = &rb_ctx3;

  threadArgs[3].path = strName;
  threadArgs[3].topicIds = topicSet4;
  threadArgs[3].minPartId = 0;
  threadArgs[3].maxPartId = nparts;
  threadArgs[3].killSelf = true;
  threadArgs[3].group = "AssignRevokeCbTest";
  threadArgs[3].id = 3;
  threadArgs[3].numMsgs = 9000;
  threadArgs[3].ctx = &rb_ctx4;

  pthread_create(&threadArgs[0].thread, NULL /*attr*/,
                  createAndStartConsumer, &threadArgs[0]);
  pthread_create(&threadArgs[1].thread, NULL /*attr*/,
                  createAndStartConsumer, &threadArgs[1]);
  pthread_create(&threadArgs[2].thread, NULL /*attr*/,
                  createAndStartConsumer, &threadArgs[2]);
  sleep (10);

  pthread_create(&threadArgs[3].thread, NULL /*attr*/,
                  createAndStartConsumer, &threadArgs[3]);

  pthread_join (threadArgs[0].thread, NULL);
  pthread_join (threadArgs[1].thread, NULL);
  pthread_join (threadArgs[2].thread, NULL);
  pthread_join (threadArgs[3].thread, NULL);
}

int ConsumerTest::runCommitTest (char * strName, const char *groupid,
                                 bool consumerInvalid, bool topicInvalid,
                                 bool offsetInvalid) {
  //produce two messages
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 15;
  p.runTest (strName, 1, 1, 1, produceMsg,
             200,RD_KAFKA_MSG_F_COPY, true, 0, false, 15, &cb);
  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  rd_kafka_conf_set(conf, "enable.auto.commit", "false", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf, "group.id", groupid , errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf, "auto.offset.reset", "earliest",
                          errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf, topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(1);
  char topic[100];
  memset ( topic, '\0', 100);
  snprintf( topic, sizeof(topic), "%s0:topic0",  strName);
  rd_kafka_topic_partition_list_add(tp_list, topic , 0);
  //subscribe to only one topic partition
  rd_kafka_subscribe (consumer, tp_list);
  int msgCount = 0;
  int   offset1=0;
  rd_kafka_topic_partition_t *rktpar;
  while (msgCount < 12) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      offset1 = rkmessage->offset;
      rd_kafka_message_destroy (rkmessage);
    }
  }
  int commitResult = 0;
  if (!offsetInvalid) {
    rd_kafka_topic_partition_list_t *offset_list =
                                   rd_kafka_topic_partition_list_new(1);
    if(topicInvalid)
      snprintf( topic, sizeof(topic), "%s0topic0", strName);

    rktpar = rd_kafka_topic_partition_list_add (offset_list, topic, 0);
    rktpar->offset = offset1+1;
    if(consumerInvalid)
      commitResult =   rd_kafka_commit (NULL, offset_list, 0);
    else
      commitResult =   rd_kafka_commit (consumer, offset_list, 0); // commit_sync
  } else {
    commitResult =   rd_kafka_commit (consumer, NULL, 0);
  }
  sleep (5);
  rd_kafka_consumer_close (consumer);
  rd_kafka_destroy (consumer);
  if(topicInvalid)
    return commitResult;
  else if (offsetInvalid) // commit all
    return 13;
  else if (consumerInvalid)
    return commitResult;
  else
    return rktpar->offset;
}

int ConsumerTest::runConsumerCloseTest (char *strName, char * groupid,
                                        bool consumerInvalid) {
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 15;
  p.runTest (strName, 1, 1, 1, produceMsg,
             200, RD_KAFKA_MSG_F_COPY, true, 0, false, 15, &cb);
  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  rd_kafka_conf_set(conf, "group.id", groupid , errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf, topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  subscribe_consumer (strName, consumer, NULL, false /*kafkaConsumer*/);
  int msgCount = 0;
  int noMsg = 0;
  if (consumerInvalid)
    return rd_kafka_consumer_close (NULL);

  while (noMsg < 10) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      if (msgCount == 5)
        rd_kafka_consumer_close (consumer);
      rd_kafka_message_destroy (rkmessage);
    }else {
        noMsg++;
    }
  }
  return msgCount;
}

int ConsumerTest::runConsumerBack2BackTest (char *strName) {
  struct timespec ts;
  int s;
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 100;
  p.runTest (strName, 1, 1, 1, produceMsg,
             200, RD_KAFKA_MSG_F_COPY, false, 0, false, 15, &cb);

  int topicSet1[1] = {0};
  struct RebalanceCbCtx rb_ctx1;
  ConsumerThreadArgs threadArgs;
  threadArgs.path = strName;
  threadArgs.topicIds = topicSet1;
  threadArgs.minPartId = 0;
  threadArgs.maxPartId = 1;
  threadArgs.killSelf = false;
  threadArgs.group = "b2bTest";
  threadArgs.id = 0;
  threadArgs.numMsgs = 1100;
  threadArgs.ctx = &rb_ctx1;
  pthread_create(&threadArgs.thread, NULL /*attr*/,
                 createAndStartConsumer, &threadArgs);
  sleep (5);
  p.runTest (strName, 1, 1, 1, 1000,
                  200, RD_KAFKA_MSG_F_COPY, false, 0, false, 15, &cb);

  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += 1000;
  s =  pthread_timedjoin_np (threadArgs.thread, NULL, &ts);
  return s;
}
bool verify_topic_partition_list (char *str,
                                rd_kafka_topic_partition_list_t *list,
                                int64_t expectedVal,
                                bool verify,
                                bool print) {
  if (print) cout << "\n" <<str;
  bool match = false;
  for (int i =0; i< list->cnt; i++) {
    if(print) {
      cout << "\n Topic:" << list->elems[i].topic;
      cout << "\t Partition:" << list->elems[i].partition;
      cout << "\t Offset:" << list->elems[i].offset;
    }
    if(expectedVal && verify) {
      if(expectedVal == list->elems[i].offset )
        match = true;
      else {
        match = false;
        cerr << "\nERROR:" << str << " Offset expected: "<< expectedVal;
        cerr << " , returned:" << list->elems[i].offset << " partition " << i;
      }
    }
  }
  return match;
}
void ConsumerTest::runConsumerSeekPositionTest (char *strName, char * groupid,
                                                bool print) {
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 1000;
  //Produce 1000 messages.
  p.runTest (strName, 1, 1, 1, produceMsg,
             200, RD_KAFKA_MSG_F_COPY, false, 0, false, 15, &cb);

  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  rd_kafka_conf_set(conf, "enable.auto.commit", "false", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf, "group.id", groupid , errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf, topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *out_list =
                                    rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *commit_list =
                                    rd_kafka_topic_partition_list_new(0);
  int ntopics = 1;
  char currentName[100];
  //populate tp list for consumer
  for (int t = 0; t < ntopics; ++t) {
    memset ( currentName, '\0', 100);
    snprintf(currentName, sizeof (currentName),"%s0:topic%d",  strName, t);
    rd_kafka_topic_partition_list_add(tp_list, currentName, RD_KAFKA_PARTITION_UA);
    rd_kafka_topic_partition_list_add(out_list, currentName, 0);
  }

  rd_kafka_subscribe (consumer, tp_list);
  int msgCount = 0;
  int numMsgs = 100;
  int offset = -1;
  rd_kafka_topic_t *rkt = NULL;
  //consume 100 messages and commit the offset
  while (msgCount < numMsgs) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      if (msgCount == numMsgs){
        offset = rkmessage->offset;
        rkt = rkmessage->rkt;
      }
      rd_kafka_message_destroy (rkmessage);
    }
  }
  //commit the offset
  rd_kafka_topic_partition_t *rktpar =
    rd_kafka_topic_partition_list_add (commit_list, currentName, 0);
  rktpar->offset = offset + 1;
  rd_kafka_commit (consumer, commit_list, 0);
  sleep (3);
  rd_kafka_committed (consumer, out_list, 1000);
  bool verify = true;
  verify_topic_partition_list("Committed after 100 consumed messages.", out_list,
                              numMsgs+1, verify, print);
  rd_kafka_position (consumer, out_list);
  verify_topic_partition_list("Position after 100 consumed messages:" , out_list,
                              numMsgs+1, verify, print);
  // seek to offset 500
  rd_kafka_seek (rkt, 0, 500, 2000 );
  rd_kafka_position (consumer, out_list);
  verify_topic_partition_list("Position after seek to 500:" , out_list,
                                500, verify, print);
  msgCount = 0;
  // consume 30 messages.
  while (msgCount < 30) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      if (msgCount == numMsgs){
        offset = rkmessage->offset;
        rkt = rkmessage->rkt;
      }
      rd_kafka_message_destroy (rkmessage);
    }
  }
  rktpar->offset = offset +1;
  rd_kafka_commit (consumer, commit_list, 0);
  sleep (3);
  rd_kafka_committed (consumer, out_list, 1000);
  verify_topic_partition_list("Committed after 30 consumed messages.",  out_list,
                              101, verify, print);

  rd_kafka_position (consumer, out_list);
  verify_topic_partition_list("Position after 30 consumed messages:" ,  out_list,
                              530, verify, print);
  // seek to offset 200
  rd_kafka_seek (rkt, 0, 200, 2000 );
  rd_kafka_position (consumer, out_list);
  verify_topic_partition_list("Position after seek to 200:" , out_list,
                              200, verify, print);

  msgCount = 0;
  while (msgCount < 200) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 100);
    if (rkmessage) {
      msgCount ++;
      if (msgCount == 200){
        offset = rkmessage->offset;
        rkt = rkmessage->rkt;
      }
      rd_kafka_message_destroy (rkmessage);
    }
  }
}

rd_kafka_resp_err_t ConsumerTest::runRegexTest (char *stream1, char *stream2, int type, bool isBlackList) {
  rd_kafka_t *producer;
  char errstr[128];
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  producer = rd_kafka_new (RD_KAFKA_PRODUCER,
                            conf,
                            errstr,
                            sizeof(errstr));

  char *str1 = (char *)malloc ((strlen(stream1)+1) *sizeof(char));
  sprintf (str1, "%s0", stream1);
  char *str2 = (char *) malloc ((strlen(stream2)+1) *sizeof(char));
  sprintf (str2, "%s0", stream2);
  int numTopics = 5;
  char **str1Topic = (char **)malloc (numTopics * sizeof (char*));
  char **str2Topic = (char **)malloc (numTopics * sizeof (char*));
  rd_kafka_topic_t **topicHandle =  (rd_kafka_topic_t **)
                            malloc (2 *numTopics * sizeof (rd_kafka_topic_t *));
  rd_kafka_topic_conf_t **topicConf = (rd_kafka_topic_conf_t **)
                            malloc (2 *numTopics * sizeof (rd_kafka_topic_conf_t *));

  int maxTopicLen =10;
  char *value = "RegexTest";
  for (int i =0; i < numTopics; i++) {
    str1Topic[i] = (char*) malloc (sizeof(char) * (strlen(str1)+ maxTopicLen));
    str2Topic[i] = (char*) malloc (sizeof(char) * (strlen(str2)+ maxTopicLen));
    if (i%2 == 0) {
      sprintf(str1Topic[i], "%s:pic%d", str1, i);
      sprintf(str2Topic[i], "%s:apple%d", str2, i);
    } else {
      sprintf(str1Topic[i], "%s:banana%d", str1, i);
      sprintf(str2Topic[i], "%s:topic%d", str2, i);
    }
    topicConf[i] = rd_kafka_topic_conf_new();
    topicHandle[i] = rd_kafka_topic_new (producer, str1Topic[i], topicConf[i]);

    topicConf[(2*i) + 1] = rd_kafka_topic_conf_new();
    topicHandle[(2*i) + 1] = rd_kafka_topic_new (producer, str2Topic[i],
                                                topicConf[(2*i) + 1]);
    rd_kafka_produce (topicHandle[i], 0, 0, (void *) value, strlen(value),
                      NULL, 0, NULL);
    rd_kafka_produce (topicHandle[(2*i) + 1], 0, 0, (void *) value,
                      strlen(value), NULL, 0, NULL);
    cout << "\n" <<  i << ": " << str1Topic[i]<< ": " << str2Topic[i];
  }

  rd_kafka_topic_partition_list_t *tp_list =
                                  rd_kafka_topic_partition_list_new(0);

    char **regexTopicStream1 = (char **) malloc (2 * sizeof(char*));
    regexTopicStream1[0] = (char *)malloc((strlen(str1)+6) * sizeof (char));
    sprintf(regexTopicStream1[0], "%s:^a.*", str1);
    regexTopicStream1[1] = (char *)malloc((strlen(str1)+6) * sizeof (char));
    sprintf(regexTopicStream1[1], "%s:^p.*", str1);


    char **expectedTopicStream1 ;
    int expectedSubCnt = 0;
    if (isBlackList) {
    expectedTopicStream1 = (char **) malloc (2 * sizeof(char*));
    expectedTopicStream1[0] = (char *)malloc(strlen( str1Topic[0]) * sizeof (char));
    strcpy(expectedTopicStream1[0], str1Topic[2]);
    expectedTopicStream1[1] = (char *)malloc(strlen( str1Topic[2]) * sizeof (char));
    strcpy(expectedTopicStream1[1], str1Topic[4]);
    expectedSubCnt = 2;

    } else {
    expectedTopicStream1 = (char **) malloc (3 * sizeof(char*));
    expectedTopicStream1[0] = (char *)malloc(strlen( str1Topic[0]) * sizeof (char));
    strcpy(expectedTopicStream1[0], str1Topic[0]);
    expectedTopicStream1[1] = (char *)malloc(strlen( str1Topic[0]) * sizeof (char));
    strcpy(expectedTopicStream1[1], str1Topic[2]);
    expectedTopicStream1[2] = (char *)malloc(strlen( str1Topic[2]) * sizeof (char));
    strcpy(expectedTopicStream1[2], str1Topic[4]);
    expectedSubCnt = 3;

    }
    char **regexTopicStream2 = (char **) malloc (2 * sizeof(char*));
    regexTopicStream2[0] = (char *)malloc((strlen(str2)+6) * sizeof (char));
    sprintf(regexTopicStream2[0], "%s:^t.*", str2);
    regexTopicStream2[1] = (char *)malloc((strlen(str2)+6) * sizeof (char));
    sprintf(regexTopicStream2[1], "%s:^i.*", str2);

    char **maprTopic = (char **) malloc (2 * sizeof(char*));
    maprTopic[0]= (char *)malloc((strlen(str1)+8) * sizeof (char));
    sprintf(maprTopic[0], "%s:topic0", str1);
    maprTopic[1] = (char *)malloc((strlen(str1)+8) * sizeof (char));
    sprintf(maprTopic[1], "%s:topic1", str1);
    for (int i = 0; i <2; i++) {
      switch (type) {
        case 0: //regex topics, same stream
                rd_kafka_topic_partition_list_add(tp_list, regexTopicStream1[i],
                                                  RD_KAFKA_PARTITION_UA);
                continue;
        case 1: //streams topics and regex same stream /*Currently disabled*/
                rd_kafka_topic_partition_list_add(tp_list, maprTopic[i],
                                                  RD_KAFKA_PARTITION_UA);
                rd_kafka_topic_partition_list_add(tp_list, regexTopicStream1[i],
                                        RD_KAFKA_PARTITION_UA);
                expectedSubCnt = 0;

                continue;
        case 2: //regex diff stream
                rd_kafka_topic_partition_list_add(tp_list, regexTopicStream1[i],
                                                  RD_KAFKA_PARTITION_UA);
                rd_kafka_topic_partition_list_add(tp_list, regexTopicStream2[i],
                                                  RD_KAFKA_PARTITION_UA);
                expectedSubCnt = 0;
                continue;
        default: //stream topics only
                rd_kafka_topic_partition_list_add(tp_list, maprTopic[i],
                                                   RD_KAFKA_PARTITION_UA);

                expectedSubCnt = 2;
                continue;
      }
    }
  rd_kafka_t *consumer;
  rd_kafka_conf_t *confc = rd_kafka_conf_new();
  rd_kafka_conf_set(confc, "enable.auto.commit", "false", errstr, sizeof(errstr));
  rd_kafka_conf_set(confc, "group.id", "regexGr" , errstr, sizeof(errstr));
  char *tblacklist = (char *) malloc (200 * sizeof(char)) ;
  memset (tblacklist , 0 , 200);
  sprintf(tblacklist, "%s:a.*,%s:.*b.*,%s:p.*0", str1, str1, str1);
  if (isBlackList)
    rd_kafka_conf_set(confc, "topic.blacklist" , tblacklist,  errstr, sizeof(errstr));

  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, confc, errstr, sizeof(errstr));
  rd_kafka_resp_err_t error = rd_kafka_subscribe(consumer, tp_list);
  rd_kafka_topic_partition_list_t *outList = NULL;
  sleep (3);
  cout << "\nExpected subscription cnt: " << expectedSubCnt;
  if (type == 0) {
    for (int i =0; i < expectedSubCnt; i++)
      cout << "\n Topic:" << expectedTopicStream1[i];
  } else if (type == 3) {
    for (int i =0; i < expectedSubCnt; i++)
      cout << "\n Topic:" << maprTopic[i];
  }
  rd_kafka_subscription(consumer, &outList);
  cout << "\nSubscribed topics: " << outList->cnt;
  for (int i =0; i< outList->cnt; i++) {
      cout << "\n Topic:" << outList->elems[i].topic;
    }

  free (str1);
  free (str2);
  for (int j = 0 ; j< numTopics; j++)  {
    free (str1Topic[j]);
    free (str2Topic[j]);
  }
  free (str1Topic);
  free (str2Topic);

  for (int j = 0 ; j< 2; j++)  {
    free (regexTopicStream1[j]);
    free (regexTopicStream2[j]);
    free (maprTopic[j]);
  }

  free (regexTopicStream1);
  free (regexTopicStream2);
  free (maprTopic);

  if (!isBlackList)
    expectedSubCnt = 3;
  else
    expectedSubCnt = 2;

  for (int j = 0 ; j< expectedSubCnt; j++)
      free (expectedTopicStream1[j]);

  free (expectedTopicStream1);

 rd_kafka_topic_partition_list_destroy (tp_list);
 rd_kafka_topic_partition_list_destroy (outList);

  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy(consumer);

  return error;
}

rd_kafka_resp_err_t
ConsumerTest::runConsumerListTest (char *strName, const char *groupid,
                                   const struct rd_kafka_group_list ** glist){
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 1000;
  int ntopics = 2;
  int nparts = 4;
  char *group1 = "MultiConGr";
  char *group2 = "singleConGr";
  char *clientid1 = "Consumer1";
  char *clientid2 = "Consumer2";
  char *clientid3 = "Consumer3";
  //Produce 1000 messages per partition.
  p.runTest (strName, 1, ntopics, nparts, produceMsg,
             200, RD_KAFKA_MSG_F_COPY, false, 0, false, 15, &cb);

  //create consumer1
  rd_kafka_t *consumer1;
  rd_kafka_conf_t *conf1 = rd_kafka_conf_new();
  char defaultStr[strlen(strName) + 2];
  sprintf(defaultStr, "%s0", strName);
  char errstr[128];
  rd_kafka_conf_set(conf1, "streams.consumer.default.stream", defaultStr,
                    errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "enable.auto.commit", "true", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "client.id", clientid1, errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "group.id", group1, errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf1 = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf1, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf1, topic_conf1);
  consumer1 = rd_kafka_new (RD_KAFKA_CONSUMER, conf1, errstr, sizeof(errstr));
  //create consumer2
  rd_kafka_t *consumer2;
  rd_kafka_conf_t *conf2 = rd_kafka_conf_new();
  rd_kafka_conf_set(conf2, "streams.consumer.default.stream", defaultStr,
                    errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "enable.auto.commit", "true", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "client.id", clientid2, errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "group.id", group1, errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf2 = rd_kafka_topic_conf_new();
    rd_kafka_topic_conf_set(topic_conf2, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf2, topic_conf2);
  consumer2 = rd_kafka_new (RD_KAFKA_CONSUMER, conf2, errstr, sizeof(errstr));

  //create consumer3
  rd_kafka_t *consumer3;
  rd_kafka_conf_t *conf3 = rd_kafka_conf_new();
  rd_kafka_conf_set(conf3, "streams.consumer.default.stream", defaultStr,
                    errstr, sizeof(errstr));
  rd_kafka_conf_set(conf3, "enable.auto.commit", "true", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf3, "client.id", clientid3, errstr, sizeof(errstr));
  rd_kafka_conf_set(conf3, "group.id", group2, errstr, sizeof(errstr));
  rd_kafka_topic_conf_t *topic_conf3 = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf3, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf3, topic_conf3);
  consumer3 = rd_kafka_new (RD_KAFKA_CONSUMER, conf3, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list1 =
                                rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *tp_list2 =
                                rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *tp_list3 =
                                rd_kafka_topic_partition_list_new(0);
  char currentName[100];
  //populate tp list for consumer
  for (int t = 0; t < ntopics; ++t) {
    memset ( currentName, '\0', 100);
    snprintf(currentName, sizeof (currentName),"%s0:topic%d",  strName, t);
    if (t == 0) {
      rd_kafka_topic_partition_list_add(tp_list1, currentName, RD_KAFKA_PARTITION_UA);
      rd_kafka_topic_partition_list_add(tp_list2, currentName, RD_KAFKA_PARTITION_UA);
    }
    if (t == 1) {
      rd_kafka_topic_partition_list_add(tp_list1, currentName, RD_KAFKA_PARTITION_UA);
      rd_kafka_topic_partition_list_add(tp_list3, currentName, RD_KAFKA_PARTITION_UA);
    }
  }

  rd_kafka_subscribe (consumer1, tp_list1);
  rd_kafka_subscribe (consumer2, tp_list2);
  rd_kafka_subscribe (consumer3, tp_list3);

  int msgCount1 = 0;
  int msgCount2 = 0;
  int msgCount3 = 0;
  int totalMsg = produceMsg * ntopics * nparts;
  while ((msgCount1 < totalMsg/2) && ((msgCount2+msgCount3) < totalMsg/2)) {
    rd_kafka_message_t *rkmessage1;
    rd_kafka_message_t *rkmessage2;
    rd_kafka_message_t *rkmessage3;
    rkmessage1 = rd_kafka_consumer_poll (consumer1, 100);
    rkmessage2 = rd_kafka_consumer_poll (consumer2, 100);
    rkmessage3 = rd_kafka_consumer_poll (consumer3, 100);
    if (rkmessage1) {
      msgCount1 ++;
      rd_kafka_message_destroy (rkmessage1);
    }
    if (rkmessage2) {
      msgCount2 ++;
      rd_kafka_message_destroy (rkmessage2);
    }
    if (rkmessage3) {
      msgCount3 ++;
      rd_kafka_message_destroy (rkmessage3);
    }
  }

  rd_kafka_resp_err_t ret = RD_KAFKA_RESP_ERR_NO_ERROR;
  struct rd_kafka_group_list *grplistp;
  while (1) {
    sleep(5);
    ret = rd_kafka_list_groups (consumer1, groupid,
        (const struct rd_kafka_group_list **) &grplistp, 1000);
    break;
  }

  *glist = grplistp;
  rd_kafka_consumer_close(consumer1);
  rd_kafka_consumer_close(consumer2);
  rd_kafka_consumer_close(consumer3);

  rd_kafka_destroy(consumer1);
  rd_kafka_destroy(consumer2);
  rd_kafka_destroy(consumer3);

  return ret;
}

void nullAssigntest_rebalance_cb(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                           rd_kafka_topic_partition_list_t *partitions,
                           void *opaque) {
  switch (err)
  {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
        rd_kafka_assign (rk, partitions);
        break;
    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
        rd_kafka_assign (rk, NULL);
        break;
    default:
        break;
  }
}

rd_kafka_resp_err_t
ConsumerTest::runNullAssignTest (char *strName, int nTopics, int npart){
  Producer p;
  uint64_t cb = 0;
  int produceMsg = 1000;
  int ntopics = nTopics;
  int nparts = npart;
  char *group1 = "MultiConGr";
  char *clientid1 = "Consumer1";
  char *clientid2 = "Consumer2";
  //Produce 1000 messages per partition.
  p.runTest (strName, 1, ntopics, nparts, produceMsg,
             200, RD_KAFKA_MSG_F_COPY, false, 0, false, 15, &cb);

  //create consumer1
  rd_kafka_t *consumer1;
  rd_kafka_conf_t *conf1 = rd_kafka_conf_new();
  char defaultStr[strlen(strName) + 2];
  sprintf(defaultStr, "%s0", strName);
  char errstr[128];
  rd_kafka_conf_set(conf1, "streams.consumer.default.stream", defaultStr,
                    errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "enable.auto.commit", "true", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "client.id", clientid1, errstr, sizeof(errstr));
  rd_kafka_conf_set(conf1, "group.id", group1, errstr, sizeof(errstr));
 rd_kafka_conf_set_rebalance_cb(conf1, nullAssigntest_rebalance_cb);
  rd_kafka_topic_conf_t *topic_conf1 = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf1, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf1, topic_conf1);
  consumer1 = rd_kafka_new (RD_KAFKA_CONSUMER, conf1, errstr, sizeof(errstr));

  rd_kafka_conf_t *conf2 = rd_kafka_conf_new ();
  rd_kafka_conf_set(conf2, "streams.consumer.default.stream", defaultStr,
                    errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "enable.auto.commit", "true", errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "client.id", clientid2, errstr, sizeof(errstr));
  rd_kafka_conf_set(conf2, "group.id", group1, errstr, sizeof(errstr));
 rd_kafka_conf_set_rebalance_cb(conf2, nullAssigntest_rebalance_cb);
  rd_kafka_topic_conf_t *topic_conf2 = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set(topic_conf2, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(conf2, topic_conf2);

  rd_kafka_topic_partition_list_t *tp_list1 =
                                rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *tp_list2 =
                                rd_kafka_topic_partition_list_new(0);
  char currentName[100];
  //populate tp list for consumer
  for (int t = 0; t < ntopics; ++t) {
    memset ( currentName, '\0', 100);
    snprintf(currentName, sizeof (currentName),"%s0:topic%d",  strName, t);
    rd_kafka_topic_partition_list_add(tp_list2, currentName, -1);
    for (int p = 0; p < nparts; p+=2){
       rd_kafka_topic_partition_list_add(tp_list1, currentName, p);
    }
  }

  int err = 0;
  err = rd_kafka_assign (consumer1, NULL);
  assert (err == 0);
  cout << "Assign topic partitions" << endl;
  err = rd_kafka_assign (consumer1, tp_list1);
  sleep (5);
  assert (err == 0);
  rd_kafka_topic_partition_list_t *out_list = NULL;
  err = rd_kafka_assignment (consumer1, &out_list);
  assert (err == 0);
  assert (out_list->cnt == (nparts*ntopics /2));

  rd_kafka_topic_partition_list_destroy (out_list);
  cout << "Null assign partitions" << endl;
  err = rd_kafka_assign (consumer1, NULL);
  assert (err == 0);
  out_list = NULL;
  rd_kafka_assignment (consumer1, &out_list);
  assert (out_list->cnt == 0);
  rd_kafka_topic_partition_list_destroy (out_list);
  cout << "Consecutive null assign partitions" << endl;
  err = rd_kafka_assign (consumer1, NULL);
  assert (err == 0);
  out_list = NULL;
  rd_kafka_assignment (consumer1, &out_list);
  assert (out_list->cnt == 0);
  rd_kafka_topic_partition_list_destroy (out_list);
  out_list = NULL;
  cout << "Subscribe after null assign";
  err = rd_kafka_subscribe (consumer1, tp_list2);
  assert (err == RD_KAFKA_RESP_ERR__CONFLICT);
  cout << " failed as expected" << endl;
  rd_kafka_assignment (consumer1, &out_list);
  assert (out_list->cnt == 0);
  rd_kafka_topic_partition_list_destroy (out_list);

  out_list= NULL;
  err = rd_kafka_assign (consumer1, tp_list1);
  assert (err == 0);
  rd_kafka_assignment (consumer1, &out_list);
  assert (out_list->cnt == (nparts*ntopics /2));
  rd_kafka_topic_partition_list_destroy (out_list);
  rd_kafka_consumer_close (consumer1);
  rd_kafka_destroy (consumer1);

  consumer1 = rd_kafka_new (RD_KAFKA_CONSUMER, conf2, errstr, sizeof(errstr));

  cout << "Subscribe topic partitions" << endl;
  err = rd_kafka_subscribe (consumer1, tp_list2);
  sleep (5);
  assert (err == 0);
  out_list =  NULL;
  err = rd_kafka_assignment (consumer1, &out_list);
  assert (err == 0);
  assert (out_list->cnt == (nparts*ntopics));
  rd_kafka_topic_partition_list_destroy (out_list);

  cout << "Assign after subscribe ";
  err = rd_kafka_assign (consumer1, tp_list1);
  assert (err == 0);
  out_list= NULL;
  err = rd_kafka_assignment (consumer1, &out_list);
  assert (err == 0);
  assert (out_list->cnt == (nparts*ntopics));
  cout << "NO-OP as expected" << endl;
  rd_kafka_topic_partition_list_destroy (out_list);

  cout << "Assign with NULL partition after subscribe ";
  err = rd_kafka_assign (consumer1, NULL);
  assert (err == 0);
  out_list = NULL;
  err = rd_kafka_assignment (consumer1, &out_list);
  assert (err == 0);
  assert (out_list->cnt == (nparts*ntopics));
  cout << "NO-OP as expected" << endl;

  rd_kafka_topic_partition_list_destroy (out_list);
  rd_kafka_topic_partition_list_destroy (tp_list1);
  rd_kafka_topic_partition_list_destroy (tp_list2);
  rd_kafka_consumer_close (consumer1);
  rd_kafka_destroy (consumer1);

  return RD_KAFKA_RESP_ERR_NO_ERROR;
}

