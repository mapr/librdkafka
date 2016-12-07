#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include "ConfigTest.h"

int ConfigTest::runProducerConfigTest (char *str, char *topic,
                                       char *pDefaultStr, int numMsgs,
                                       int maxMsgSize) {
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  if (pDefaultStr) {
    char pStr[strlen(pDefaultStr) +1];
    memset (pStr, 0, strlen(pDefaultStr) +1);
    sprintf (pStr, "%s0", pDefaultStr);
    rd_kafka_conf_set(conf, "streams.producer.default.stream", pStr,
                      errstr, sizeof(errstr));
  }

  if (maxMsgSize != -1) {
    char size[16];
    memset (size, 0, sizeof(size));
    snprintf(size, sizeof (size), "%d", maxMsgSize );
    rd_kafka_conf_set(conf, "message.max.bytes", size,
                              errstr, sizeof(errstr));
  }
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  try {
    producer = rd_kafka_new (RD_KAFKA_PRODUCER,
                            conf,
                            errstr,
                            sizeof(errstr));
    if (!producer) {
      std::cerr << "\nProducerCreateTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
    }
    char topicName[30];
    memset (topicName, 0, 30);
    if(str)
      sprintf (topicName, "%s0:%s", str, topic);
    else
      sprintf (topicName, "%s", topic);

    rd_kafka_topic_t *topic_t = rd_kafka_topic_new (producer, topicName, topicConf);

    int valueSz = 10;
    if (maxMsgSize != -1)
      valueSz = maxMsgSize + 100;
    int err = 0;

    for (int i = 0; i < numMsgs; i++) {
      char *value = (char *) malloc (valueSz * sizeof (char));
      if (maxMsgSize != -1) {
        memset (value, 'a', valueSz);
      } else {
        memset (value, 0, valueSz);
        snprintf (value, valueSz, "v%d", i);
      }
      err = rd_kafka_produce (topic_t,
                                0,
                                RD_KAFKA_MSG_F_COPY,(void *) value,
                                strlen (value),
                                NULL, 0,
                                NULL);
      free (value);
    }
    rd_kafka_destroy(producer);

    return err;

  } catch (const std::exception& e) {
    std::cerr << "\nProducerCreateTest: Exception occured," << e.what() << "\n";
    return INVALID_ARGUMENT;
  }
}
static int errCount = 0;
static int cbCount = 0;
void error_cb (rd_kafka_t *rk, int err, const char *reason, void *opaque) {
  if (err == RD_KAFKA_RESP_ERR__BAD_MSG)
    errCount++;
}

void consume_cb (rd_kafka_message_t * rkmessage, void *opaque) {
  if(rkmessage)
    cbCount++;
}

int ConfigTest::runConsumerRecvMaxMsgSizeConfigTest (char *str, char *topic,
                                                     int numMsgs, int msgSize,
                                                     int recvMaxMsgSize,
                                                     bool mixSizes ) {

  rd_kafka_t *producer;
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  char errstr[128];
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  if (!producer) {
    std::cerr << "\nProducerCreateTest: Failed to create new producer\n" <<  errstr;
    return PRODUCER_CREATE_FAILED;
  }
  char topicName[30];
  memset (topicName, 0, 30);
  if(str)
    sprintf (topicName, "%s0:%s", str, topic);
  else
    sprintf (topicName, "%s", topic);
  rd_kafka_topic_t *topic_t = rd_kafka_topic_new (producer, topicName, topicConf);
  //Produce numMsgs messages.
  int produceMsgSize = msgSize;
  for (int i = 0; i < numMsgs; i++) {
    if(mixSizes && (recvMaxMsgSize <  msgSize)) {
      //produce half of the messages of size < recvMaxMsgSize
      if(i%2 == 0)
         produceMsgSize = recvMaxMsgSize - 100;
      else
        produceMsgSize = msgSize;
    }
    char *value = (char *) malloc (produceMsgSize * sizeof (char));
    memset (value, 'a', produceMsgSize);
    rd_kafka_produce (topic_t, 0, RD_KAFKA_MSG_F_COPY,(void *) value,
                              strlen (value), NULL, 0, NULL);
    free (value);
  }

  rd_kafka_destroy(producer);
  //create a consumer
  rd_kafka_t *consumer;
  rd_kafka_conf_t *c_conf = rd_kafka_conf_new();

  if (recvMaxMsgSize != -1) {
    char size[16];
    memset (size, 0, sizeof(size));
    snprintf(size, sizeof (size), "%d", recvMaxMsgSize );
    rd_kafka_conf_set(c_conf, "receive.message.max.bytes", size,
                              errstr, sizeof(errstr));
  }

  rd_kafka_conf_set(c_conf, "group.id", "recvConfig", errstr, sizeof(errstr));

  rd_kafka_conf_set_consume_cb (c_conf, consume_cb);
  rd_kafka_conf_set_error_cb (c_conf, error_cb);
  rd_kafka_topic_conf_t *def_topic_conf = rd_kafka_topic_conf_new();
  rd_kafka_topic_conf_set (def_topic_conf, "auto.offset.reset", "earliest",
                                             errstr, sizeof(errstr));
  rd_kafka_conf_set_default_topic_conf(c_conf, def_topic_conf);
  consumer = rd_kafka_new (RD_KAFKA_CONSUMER, c_conf, errstr, sizeof(errstr));
  rd_kafka_topic_partition_list_t *tp_list = rd_kafka_topic_partition_list_new(0);
  rd_kafka_topic_partition_list_add (tp_list, topicName, RD_KAFKA_PARTITION_UA);
  rd_kafka_subscribe (consumer, tp_list);
  rd_kafka_poll_set_consumer (consumer);
  cbCount = 0;
  errCount = 0;
  while (cbCount < numMsgs) {
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll(consumer, 1000);
    if (errCount >= numMsgs)
      break;
    if (mixSizes && errCount >= numMsgs/2)
      break;
  }
  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy(consumer);
  return errCount;
}

