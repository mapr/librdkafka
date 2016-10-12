#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include "Producer.h"

int ProducerTest::runProducerCreateTest(bool isConfValid,
                                          bool isTypeValid) {
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf;

  if(isConfValid)
    conf = rd_kafka_conf_new();
  else
    conf = NULL;

  rd_kafka_type_t type;
  char errstr[512];

  if(isTypeValid)
    type = RD_KAFKA_PRODUCER;
  else
    type = (rd_kafka_type_t) NULL;

  try {
    producer = rd_kafka_new(type,
                            conf,
                            errstr,
                            sizeof(errstr));
    if (!producer) {
      cerr << "\nProducerCreateTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
    }
    rd_kafka_destroy(producer);
    return SUCCESS;
  } catch (const std::exception& e) {
    cerr << "\nProducerCreateTest: Exception occured," << e.what() << "\n";
    return INVALID_ARGUMENT;
  }
}

int ProducerTest::runProduceTest (const char *topicName,
                                   bool isConfValid,
                                   const char *key,
                                   const char *value,
                                   int msgFlag,
                                   int pid) {
  static char *brokers = "localhost:9092";
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf;
  if(isConfValid)
    conf = rd_kafka_conf_new();
  else
    conf = NULL;

  rd_kafka_topic_t *topic;
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  char errstr[512];
  try {
    producer = rd_kafka_new (RD_KAFKA_PRODUCER,
                            conf,
                            errstr,
                            sizeof(errstr));
    if (!producer) {
      cerr << "\nProducerCreateTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
    }

    rd_kafka_brokers_add(producer, brokers);
    topic = rd_kafka_topic_new (producer, topicName, topicConf);

    int keylen = key?strlen(key):0;
    int vallen = value?strlen(value):0;

    int err = rd_kafka_produce (topic,
                                pid,
                                msgFlag,(void *) value,
                                vallen,
                                (const void *)key, keylen,
                                NULL);

    rd_kafka_destroy(producer);
    if(err) {
       return PRODUCER_SEND_FAILED; 
    }
    return SUCCESS;
  } catch (const std::exception& e) {
    cerr << "\nProducerCreateTest: Exception occured," << e.what() << "\n";
    return INVALID_ARGUMENT;
  }
}

void *ProducerRun(void *arg)
{
  ProducerThreadArgs *pt = static_cast<ProducerThreadArgs *> (arg);
  ((pt->retdata)->retCode) = (pt->producer)->run(&((pt->retdata)->numCb));
  return (void *) pt->retdata;
}

int ProducerTest::runProducerCombinationTest(char *path, int nstreams,
                                              int ntopics,int nparts, int nmsgs,
                                              int nproducers, int msgsize,
                                              bool roundRb, int nslowtopics,
                                              bool print, uint64_t timeout,
                                              uint64_t *numCallbacks){
  int globalRes = 0;

  Producer *producerArr[nproducers];
  struct retData retDataArr[nproducers];
  ProducerThreadArgs threadArgs[nproducers];
  for (int p = 0; p < nproducers; ++p) {
    producerArr[p] = new Producer(path, nstreams, ntopics, nparts, nmsgs,
                                 msgsize, RD_KAFKA_MSG_F_COPY, roundRb,
                                 nslowtopics, print, timeout);
    ProducerThreadArgs *pt = &threadArgs[p];
    pt->producer = producerArr[p];
    pt->retdata = retDataArr + p;
    pthread_create(&pt->thread, NULL /*attr*/, ProducerRun, pt);
  }
  for (int i = 0; i < nproducers; ++i) {
    void *ret;
    pthread_join(threadArgs[i].thread, &ret);
    //Get Data in Struct for future use to display /report per user report.
    retDataArr[i].retCode = ((struct retData *)ret)->retCode;
    retDataArr[i].numCb = ((struct retData *)ret)->numCb;
    if (print) {
      cout << "\nproducer"<< i << ":";
      cout << "\treturn code: " << retDataArr[i].retCode;
      cout << "\tReceived callbacks: " << retDataArr[i].numCb;
    }
    atomic_add64(numCallbacks, retDataArr[i].numCb);
    atomic_add64(&globalRes, retDataArr[i].retCode);
  }
  for (int p = 0; p < nproducers; ++p) {
    delete producerArr[p];
  }
  return globalRes;
}

int ProducerTest::runProducerErrorTest(char * strName, int numMsgs,
                                       bool streamDelete ) {

  int msgFlag = 0;
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf;
  conf = rd_kafka_conf_new();
  rd_kafka_topic_t *topic;
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  char errstr[512];
  try {
    producer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr)); 
    if (!producer) {
      cerr << "\nProducerErrorTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
    }
    char topicName[100];
    sprintf(topicName, "%s0:topic", strName);
    topic = rd_kafka_topic_new (producer, topicName, topicConf);
    int err = 0;
    char value[30];
    int deleteCount = 100;
    for(int nmsgs = 0; nmsgs <numMsgs; nmsgs++) {
      if(nmsgs % deleteCount == 0) {
        if (streamDelete)
          stream_delete(strName , 1);
        //else
          //TODO:Delete streams topic instead of rd_kafka_topic_destroy(topic);
      }
      sprintf(value, "value:%d", nmsgs);
      err = rd_kafka_produce (topic,
                                0,
                                msgFlag,(void *) value,
                                strlen(value),
                                NULL, 0,
                                NULL);
      if(err)
        break;
    }
    rd_kafka_destroy(producer);
    return err;
  } catch (const std::exception& e) {
    cerr << "\nProducerErrorTest: Exception occured," << e.what() << "\n";
    return INVALID_ARGUMENT;
  }
}

void msg_delivered_cb (rd_kafka_t *rk,
                              const rd_kafka_message_t *rkmessage,
                              void *opaque) {
  Producer::CallbackCtx *myCtx = (Producer::CallbackCtx *) rkmessage->_private;
  uint64_t now = 0;
  if (myCtx->start)
    now = CurrentTimeMillis();

  myCtx->stats->report(now - myCtx->start, myCtx->bytes);
  delete myCtx;

};

int ProducerTest::runProducerMixedTopicTest(char * strName, int type){
  rd_kafka_t *producer;
  int err = SUCCESS;
  char errstr[512];
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb);
  //Create Producer
  if (!(producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                                errstr, sizeof(errstr)))){
    cerr << " Failed to create new producer\n" <<  errstr;
    return PRODUCER_CREATE_FAILED;
  }

  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  //create Mapr topic
  char maprTopicName[100];
  sprintf(maprTopicName, "%s0:maprtopic",  strName);
  rd_kafka_topic_t *maprTopicObj = rd_kafka_topic_new (producer, maprTopicName,
                                            rd_kafka_topic_conf_dup(topicConf));
  //create kafka topic
  char kafkaTopicName[100] = "kafkatopic";
  rd_kafka_topic_t *kafkaTopicObj = rd_kafka_topic_new (producer, kafkaTopicName,
                                            rd_kafka_topic_conf_dup(topicConf));

  char *key;
  char *value;
  switch (type) {
  case 0: //mapr producer
          key = "maprProduceMaprTopicKey";
          value = "maprProducerMaprTopicValue";
          err = rd_kafka_produce(maprTopicObj, 0, 0, value,
                                strlen(value), key, strlen(key), NULL);
          if(err) {
            rd_kafka_topic_destroy(maprTopicObj);
            rd_kafka_topic_destroy(kafkaTopicObj);
            rd_kafka_destroy(producer);
            return PRODUCER_SEND_FAILED;
          }
          key = "maprProducerKafkaTopicKey";
          value = "maprProducerKafkaTopicValue";
          err= rd_kafka_produce(kafkaTopicObj, 0, 0, value,
                                strlen(value), key, strlen(key), NULL);
          break;
  case 1://kafka producer
          key = "kafkaProduceKafkaTopicKey";
          value = "kafkaProducerKafkaTopicValue";
          err = rd_kafka_produce(kafkaTopicObj, 0, 0, value,
                                strlen(value), key, strlen(key), NULL);
          if(err) {
            rd_kafka_topic_destroy(maprTopicObj);
            rd_kafka_topic_destroy(kafkaTopicObj);
            rd_kafka_destroy(producer);
            return PRODUCER_SEND_FAILED;
          }
          key = "kafkaProducerMaprTopicKey";
          value = "kafkaProducerMaprTopicValue";
          err = rd_kafka_produce(maprTopicObj, 0, 0, value,
                                strlen(value), key, strlen(key), NULL);
          break;
  default:
        break;
  }

  rd_kafka_topic_destroy(maprTopicObj);
  rd_kafka_topic_destroy(kafkaTopicObj);
  rd_kafka_destroy(producer);
  return err;
}
