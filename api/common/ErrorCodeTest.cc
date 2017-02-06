#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include "ErrorCodeTest.h"

void msg_delivered_cb (rd_kafka_t *rk,
                              const rd_kafka_message_t *rkmessage,
                              void *opaque) {
  ErrStruct *errStruct =  (ErrStruct *) opaque;
  errStruct->apiCbErr = (int)rkmessage->err;
}

ErrStruct* ErrorCodeTest::runRdKafkaProduceApiErrorCodeTest(const char * topic,
                                    int32_t partition, int msgflags, void *payload,
                                    size_t len, const void *key, size_t keylen,
                                    void *msg_opaque, void *ctx)  {

  rd_kafka_t *producer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb);
  rd_kafka_conf_set_opaque (conf, ctx);
  rd_kafka_topic_t *topic_t = NULL;
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  char errstr[512];
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  rd_kafka_brokers_add(producer, KAFKA_BROKERS);
  int err = 0;
  if (topic) {
    topic_t = rd_kafka_topic_new (producer, topic, topicConf);
    err = rd_kafka_produce (topic_t, partition, msgflags, payload, len,
                           key, keylen, NULL);
  } else {
    err = rd_kafka_produce (NULL, partition, msgflags, payload, len,
                           key, keylen, NULL);

  }
  sleep (2);
  rd_kafka_poll (producer, 100);

  ErrStruct *errStruct = (ErrStruct *)rd_kafka_opaque (producer);
  errStruct->apiErr = err;
  errStruct->errCode = rd_kafka_last_error();

  if (topic_t) rd_kafka_topic_destroy (topic_t);
  rd_kafka_destroy(producer);
  sleep (5);
  return errStruct;

}

void err_rebalance_cb(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                           rd_kafka_topic_partition_list_t *partitions,
                           void *opaque) {
  ErrStruct *errStruct =  (ErrStruct *) opaque;
  errStruct->apiCbErr = (int)err;
  switch (err)
  {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
          errStruct->assignedPartition = rd_kafka_topic_partition_list_copy (partitions);
          break;
    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
          errStruct->revokedPartition = rd_kafka_topic_partition_list_copy (partitions);
          break;
    default:
          break;
  }
}

void err_commit_cb (rd_kafka_t *rk, rd_kafka_resp_err_t err,
                          rd_kafka_topic_partition_list_t *offsets,
                          void *opaque) {

  printf ("\n err_commit_cb called: err: %d", (int) err);

  for (int i =0; i < offsets->cnt; i++) {
    printf ("\noffsets: %s ",(char *) offsets->elems[i].topic);
    printf ("\t err: %d ",(int) offsets->elems[i].err);
  }
}
ErrStruct* ErrorCodeTest::runSubscribeErrorCodeTest(char *strName, char *topic,
                                             bool isKafka, bool isAssign,
                                             bool addNonExistentTopic, bool commit,
                                             void *ctx) {
  char errstr[512];
  //Produce to topic
  rd_kafka_t *producer;
  rd_kafka_conf_t *pconf = rd_kafka_conf_new();
  rd_kafka_topic_t *topic_t;
  char *value = "value";
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, pconf, errstr, sizeof(errstr));
  rd_kafka_brokers_add(producer, KAFKA_BROKERS);

  rd_kafka_t *consumer;
  int retVal=0;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  rd_kafka_conf_set (conf, "group.id", "SubscribeErrorCodeTest", errstr, sizeof(errstr));
  rd_kafka_conf_set (conf, "enable.auto.commit", commit?"true":"false", errstr, sizeof(errstr));
  rd_kafka_conf_set_rebalance_cb (conf, err_rebalance_cb);
  rd_kafka_conf_set_opaque (conf, ctx);
  if (commit)
    rd_kafka_conf_set_offset_commit_cb(conf, err_commit_cb);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  rd_kafka_brokers_add(consumer, KAFKA_BROKERS);
  rd_kafka_topic_partition_list_t *outList =
                                    rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *tp_list =
                                    rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *tp_commit_list =
                                    rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_t *committed_list =
                                    rd_kafka_topic_partition_list_new(0);
  if(isKafka) {
    rd_kafka_topic_partition_list_add(tp_list, topic, 0);
    topic_t = rd_kafka_topic_new (producer, topic, topicConf);
    rd_kafka_produce (topic_t, 0, 0, (void *)value, strlen (value), NULL, 0, NULL);
    if  (commit) {
        rd_kafka_topic_partition_list_add(committed_list, topic, 0);
        rd_kafka_topic_partition_t *rktpar =
        rd_kafka_topic_partition_list_add(tp_commit_list, topic, 0);
        rktpar->offset = 1;
    }

    if (addNonExistentTopic) {
        rd_kafka_topic_partition_list_add(tp_list, "NonExistantTopic", 0);
        if  (commit) {
          rd_kafka_topic_partition_list_add(committed_list, "NonExistantTopic", 0);
          rd_kafka_topic_partition_t *rktpar_non =
          rd_kafka_topic_partition_list_add(tp_commit_list, "NonExistantTopic", 0);
          rktpar_non->offset = 1;
        }
    }

  } else {
    char currName[200];
    snprintf (currName, sizeof(currName), "%s0:%s", strName, topic);
    rd_kafka_topic_partition_list_add(tp_list, currName, 0);
    topic_t = rd_kafka_topic_new (producer, currName, topicConf);
    rd_kafka_produce (topic_t, 0, 0, (void *)value, strlen (value), NULL, 0, NULL);
    if  (commit) {
        rd_kafka_topic_partition_list_add(committed_list, currName, 0);
        rd_kafka_topic_partition_t *rktpar_streams =
          rd_kafka_topic_partition_list_add(tp_commit_list, currName, 0);
        rktpar_streams->offset = 1;
    }
    if (addNonExistentTopic) {
      char tName[200];
      snprintf (tName, sizeof(tName), "%s0:NonExistantTopic", strName);
      rd_kafka_topic_partition_list_add(tp_list, tName, 0);
      if  (commit) {
          rd_kafka_topic_partition_list_add(committed_list, tName, 0);
          rd_kafka_topic_partition_t *rktpar_non_streams =
          rd_kafka_topic_partition_list_add(tp_commit_list, tName, 0);
          rktpar_non_streams->offset = 1;
      }
    }
  }

  if (isAssign) {
     retVal = rd_kafka_assign(consumer, tp_list);
     rd_kafka_assignment(consumer, &outList);
  } else {
     retVal = rd_kafka_subscribe(consumer, tp_list);
     rd_kafka_subscription(consumer, &outList);
  }

  ErrStruct *errStruct = (ErrStruct *)rd_kafka_opaque (consumer);
  errStruct->apiErr = retVal;
  errStruct->errCode = rd_kafka_last_error();

  if(tp_list)
    rd_kafka_topic_partition_list_destroy(tp_list);
  if(outList)
    rd_kafka_topic_partition_list_destroy(outList);


  rd_kafka_message_t *rkmessage;
  rkmessage = rd_kafka_consumer_poll (consumer, 1000);

  sleep (5);

  if (commit) {
    int commitResult = rd_kafka_commit (consumer, tp_commit_list, 0);
    rd_kafka_committed (consumer, committed_list, 1000);
    errStruct->apiErr = commitResult;
    errStruct->errCode = rd_kafka_last_error();
  }

  rd_kafka_topic_destroy (topic_t);
  rd_kafka_destroy (producer);
  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy (consumer);

  return errStruct;
}

int ErrorCodeTest::runPartitionEOFErrorCodeTest(char *strName, char *topic,
                                             bool isKafka, void *ctx) {
  char errstr[512];
  //Produce to topic
  rd_kafka_t *producer;
  rd_kafka_conf_t *pconf = rd_kafka_conf_new();
  rd_kafka_topic_t *topic_t;
  char *value = "value";
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, pconf, errstr, sizeof(errstr));
  rd_kafka_brokers_add(producer, KAFKA_BROKERS);

  rd_kafka_t *consumer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  rd_kafka_conf_set (conf, "group.id", "SubscribeErrorCodeTest", errstr, sizeof(errstr));
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  rd_kafka_brokers_add(consumer, KAFKA_BROKERS);
  rd_kafka_topic_partition_list_t *tp_list =
                                rd_kafka_topic_partition_list_new(0);


  if (isKafka) {
    rd_kafka_topic_partition_list_add(tp_list, topic, 0);
    topic_t = rd_kafka_topic_new (producer, topic, topicConf);
    rd_kafka_produce (topic_t, 0, 0, (void *)value, strlen (value), NULL, 0, NULL);
  } else {
    char currName[200];
    snprintf (currName, sizeof(currName),
        "%s0:%s", strName, topic);
    rd_kafka_topic_partition_list_add(tp_list, currName, 0);
    topic_t = rd_kafka_topic_new (producer, currName, topicConf);
    rd_kafka_produce (topic_t, 0, 0, (void *)value, strlen (value), NULL, 0, NULL);
  }

  int retVal = rd_kafka_subscribe(consumer, tp_list);

  if(tp_list)
    rd_kafka_topic_partition_list_destroy(tp_list);

  int eoferr = 0;
  while (true) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll (consumer, 1000);
    if (rkmessage) {
      if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
        eoferr = rkmessage->err;
        rd_kafka_message_destroy(rkmessage);
        break;
      }
      rd_kafka_message_destroy(rkmessage);
    }
  }
  sleep (5);
  rd_kafka_topic_destroy (topic_t);
  rd_kafka_destroy (producer);
  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy (consumer);

  return eoferr;

}
