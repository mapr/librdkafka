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
                                rd_kafka_topic_partition_list_t **outList) {
  for (int s = 0; s <  numStreams; ++s) {
    for (int t = 0; t < numTopics; ++t) {
      char currentName[100];
      memset ( currentName, '\0', 100);
      int tempIndex = s* numTopics + t;
      switch (topicType)  {
        case 0: sprintf(currentName, "%s%d:topic%d",  strName, s, t);
                break;
        case 1: sprintf(currentName, "%s%dtopic%d",  strName, s, t);
                break;
        case 2: //50-50 Mapr and Kafka topics`
                if( tempIndex % 2 == 0)
                  sprintf(currentName, "%s%d:topic%d",  strName, s, t);
                else
                  sprintf(currentName, "%s%dtopic%d",  strName, s, t);
                break;
      }
      if(isAssign) {
        for(int p = 0; p < numPartitions ; ++p )
          rd_kafka_topic_partition_list_add(*outList, currentName , p);
      } else {
          rd_kafka_topic_partition_list_add(*outList, currentName,
                                            RD_KAFKA_PARTITION_UA);
      }

    }
  }
}

void subscribe_consumer (char *strName, rd_kafka_t *consumer, bool kafkaConsumer) {
  rd_kafka_topic_partition_list_t *init_list =
                                         rd_kafka_topic_partition_list_new(1);
  if (kafkaConsumer)
    populateTopicPartitionList(strName, 1, 1, 1, 1, false, &init_list);
  else
    populateTopicPartitionList(strName, 1, 1, 1, 0, false, &init_list);

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
                                    bool isConsumerValid, int consumerType,
                                    int topicType, const char *groupId,
                                    bool isAssign) {
    rd_kafka_t *consumer;
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    char errstr[128];
    if (groupId)
      rd_kafka_conf_set(conf, "group.id", groupId, errstr, sizeof(errstr));
    rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);
    if (isConsumerValid)
      consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    else
      consumer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    int numPartitions  = 1;
    int nTotalTp = numStreams * numTopics * numPartitions;
    rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(nTotalTp);
    int retVal = SUCCESS;
    if (consumerType == 1)// mapr consumer
      subscribe_consumer (strName, consumer, false);
    else if (consumerType == 2)// kafka consumer
      subscribe_consumer (strName, consumer, true);
    else// default consumer - not mapr/kafka
      ;

    switch (topicType) {
    case 0: // All Mapr topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 0, isAssign, &tp_list);
            break;

    case 1: // All kafka topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 1, isAssign, &tp_list);
            break;

    case 2: // Mixed topics
            populateTopicPartitionList(strName, numStreams, numTopics,
                                       numPartitions, 2, isAssign, &tp_list);
            break;
    default:
            retVal = INVALID_ARGUMENT;
            break;
    }

    if (isAssign)
      cerr << "rd_kafka_assign not implemented yet"; 
    else
      retVal = rd_kafka_subscribe(consumer, tp_list);

    rd_kafka_topic_partition_list_destroy(tp_list);
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
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  //Subscribe to topics
  int nTotalTp = nstreams * ntopics * nparts;
  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(nTotalTp);
  populateTopicPartitionList(path, nstreams, ntopics,
                                       nparts, 0, false, &tp_list);
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
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(1);
  int ntopics = 1;
  //populate tp list for consumer 1 and consumer 2
  for (int t = 0; t < ntopics; ++t) {
    char currentName[100];
    memset ( currentName, '\0', 100);
    sprintf(currentName, "%s0:topic%d",  strName, args->topicIds[t]);
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
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));

  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(1);
  char topic[100];
  memset ( topic, '\0', 100);
  sprintf( topic, "%s0:topic0",  strName);
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
      sprintf( topic, "%s0topic0",  strName);

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
    return produceMsg + 1;
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
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  subscribe_consumer (strName, consumer, false /*kafkaConsumer*/);
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

