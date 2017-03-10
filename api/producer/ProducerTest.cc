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

    rd_kafka_brokers_add(producer, KAFKA_BROKERS);
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
                                              int nproducers, int msgsize, int flag,
                                              bool roundRb, int nslowtopics,
                                              bool print, uint64_t timeout,
                                              uint64_t *numCallbacks){
  int globalRes = 0;

  Producer *producerArr[nproducers];
  struct retData retDataArr[nproducers];
  ProducerThreadArgs threadArgs[nproducers];
  for (int p = 0; p < nproducers; ++p) {
    producerArr[p] = new Producer(path, nstreams, ntopics, nparts, nmsgs,
                                 msgsize, flag, roundRb,
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
    snprintf(topicName, sizeof(topicName), "%s0:topic", strName);
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
      snprintf(value, sizeof(value), "value:%d", nmsgs);
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

int ProducerTest::runProducerMixedTopicTest(char * strName, int type, int flag){
  rd_kafka_t *producer;
  int err = SUCCESS;
  char errstr[512];
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  //Create Producer
  if (!(producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                                errstr, sizeof(errstr)))){
    cerr << " Failed to create new producer\n" <<  errstr;
    return PRODUCER_CREATE_FAILED;
  }

  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  //create Mapr topic
  char maprTopicName[100];
  snprintf(maprTopicName, sizeof(maprTopicName), "%s0:maprtopic",  strName);
  rd_kafka_topic_t *maprTopicObj = rd_kafka_topic_new (producer, maprTopicName,
                                            rd_kafka_topic_conf_dup(topicConf));

  //create kafka topic
  char kafkaTopicName[100] = "kafkatopic";
  rd_kafka_topic_t *kafkaTopicObj = rd_kafka_topic_new (producer, kafkaTopicName,
                                            rd_kafka_topic_conf_dup(topicConf));

  // Creating a kafka topic for a streams producer is not allowed.
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG, rd_kafka_last_error());
  EXPECT_EQ(EINVAL, errno);

  rd_kafka_topic_destroy(maprTopicObj);
  rd_kafka_destroy(producer);

  producer = NULL;
  maprTopicObj = NULL;
  conf = NULL;

  conf = rd_kafka_conf_new();
   //Create Producer
  if (!(producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                                errstr, sizeof(errstr)))){
    cerr << " Failed to create new producer\n" <<  errstr;
    return PRODUCER_CREATE_FAILED;
  }

  kafkaTopicObj = rd_kafka_topic_new (producer, kafkaTopicName,
                                      rd_kafka_topic_conf_dup(topicConf));


  maprTopicObj = rd_kafka_topic_new (producer, maprTopicName,
                                     rd_kafka_topic_conf_dup(topicConf));

  // Creating a streams topic for a kafka producer is not allowed.
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG, rd_kafka_last_error());
  EXPECT_EQ(EINVAL, errno);

  rd_kafka_topic_destroy(kafkaTopicObj);
  rd_kafka_destroy(producer);
  return err;
}

int ProducerTest::runProducerBatchTest (const char *strName,
                                        char *topicName, int startPartId,
                                        int numPart, int msgFlags, int totalMsgs) {

  rd_kafka_topic_t *rkt;
  rd_kafka_conf_t *conf;
  rd_kafka_t *rk;
  char errstr[512];
  char msg[128];
  int i;
  rd_kafka_message_t *rkmessages;
  int retCount = 0;
  int ret = -1;
  conf = rd_kafka_conf_new();
  char streamTopic[200];
  snprintf(streamTopic, sizeof(streamTopic), "%s0:%s", strName, topicName);
  rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  rkt = rd_kafka_topic_new(rk, streamTopic, rd_kafka_topic_conf_new());

  if (startPartId != -1) {
    rkmessages = (rd_kafka_message_t*) calloc(sizeof(*rkmessages), totalMsgs / numPart);
    for (int partition = startPartId ;
            partition < (startPartId + numPart) ; partition++) {
      int batch_cnt = totalMsgs / numPart;

      for (i = 0 ; i < batch_cnt ; i++) {
        snprintf(msg, sizeof(msg), "Value:%s:%d:%d", streamTopic, partition, i);
        rkmessages[i].payload   = strdup(msg);
        rkmessages[i].len       = strlen(msg);
      }

      ret = rd_kafka_produce_batch (rkt, partition, msgFlags,
                                           rkmessages, batch_cnt);
      if (ret == -1)
        cerr << "\n Failed to produce to partition " << partition;
      else
        retCount += ret;

      if (msgFlags & RD_KAFKA_MSG_F_COPY) {
        for (int j = 0; j< batch_cnt; j++) 
          free (rkmessages[j].payload);
      }
    }
  } else {
    rkmessages = (rd_kafka_message_t*) calloc(sizeof(*rkmessages), totalMsgs);
    for (i = 0 ; i < totalMsgs ; i++) {
        snprintf(msg,  sizeof(msg), "Value:%s:%d", streamTopic, i);
        rkmessages[i].payload   = strdup(msg);
        rkmessages[i].len       = strlen(msg);
      }

      ret = rd_kafka_produce_batch (rkt, startPartId, msgFlags,
                                           rkmessages, totalMsgs);
      if (ret == -1)
        cerr << "\n Failed to produce to partition " << startPartId;
      else
        retCount += ret;
      
      if (msgFlags & RD_KAFKA_MSG_F_COPY) {
        for (int j = 0 ; j < totalMsgs; j++) {
          free (rkmessages[j].payload);
        }
      }
  }

  free(rkmessages);
  rd_kafka_topic_destroy(rkt);
  rd_kafka_destroy(rk);
  return retCount ;
}
void msg_delivered_cb_stub (rd_kafka_t *rk,
                       const rd_kafka_message_t *rkmessage,
                       void *opaque) {
  bool *print = (bool *) opaque;
  if(*print) {
    cout << "\n Partition: "  << rkmessage->partition ;
    cout << "\t Key: " << (char *)rkmessage->key;
    cout << "\t Payload: "  << (char *) rkmessage->payload ;
  }
};

int ProducerTest::runProduceOutqLenTest (const char *stream, int numStreams,
                                         int numTopics, int numParts,
                                         int numMsgsPerPartition, bool isCbConfigured,
                                         bool poll, uint64_t timeout){
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf;
  conf = rd_kafka_conf_new();
  bool print = false;
  char errstr[512];
  if (isCbConfigured)
    rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb_stub);
  rd_kafka_conf_set_opaque (conf, (void *) &print);
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));

  if (!producer) {
      cerr << "\nrunProduceOutqLenTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
  }

  rd_kafka_topic_t **topicArr = (rd_kafka_topic_t **) malloc (numStreams *
                                  numTopics* sizeof(rd_kafka_topic_t *));
  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  for (int s = 0; s <  numStreams; ++s) {
     for (int i = 0; i <  numTopics; ++i) {
      char currentName[100];
      int tempIndex = s * numTopics +i;
      snprintf(currentName,  sizeof(currentName), "%s%d:topic%d",  stream, s, i);
      topicArr[tempIndex] = rd_kafka_topic_new( producer, currentName,
                                        rd_kafka_topic_conf_dup(topicConf));
      }
  }

  for (int sIdx = 0; sIdx <  numStreams; sIdx++) {
    for (int tIdx = 0; tIdx < numTopics; tIdx++) {
      rd_kafka_topic_t *topicObj = topicArr[sIdx * numTopics + tIdx];
      for (int pIdx = 0; pIdx <  numParts; pIdx++) {
        for (int mIdx = 0; mIdx <  numMsgsPerPartition; mIdx++) {
            char sendkey[200];
            char sendvalue[200];

            memset (sendkey, '\0', 200);
            snprintf(sendkey, sizeof(sendkey), "Key:%s%d:topic%d:%d:%d:%d:%d",stream,
                                            sIdx, tIdx,sIdx, tIdx, pIdx, mIdx);
            size_t keySize = (size_t)strlen(sendkey);
            memset(sendvalue, 'a' , 200);
            snprintf(sendvalue, sizeof(sendvalue),"Value:%s%d:%d:%d:%d",stream,
                                            sIdx, tIdx, pIdx, mIdx);
            size_t valSize = 200;

            rd_kafka_produce(topicObj, pIdx, RD_KAFKA_MSG_F_COPY, sendvalue,
                                      valSize, sendkey, keySize, NULL);

        }
      }
    }
  }

  uint64_t startTime = CurrentTimeMillis();
  uint64_t currentTime = startTime;
  bool testTO = false;
  while (rd_kafka_outq_len(producer) > 0) {
    if(poll)
      rd_kafka_poll(producer, 1);

    currentTime = CurrentTimeMillis();
    if (currentTime - startTime > timeout) {
      testTO = true;
      break;
    }
  }
  for (int s = 0; s <  numStreams * numTopics; ++s)
   rd_kafka_topic_destroy(topicArr[s]);

  rd_kafka_destroy(producer);

  if (testTO)
    return TEST_TIMED_OUT;
  return 0;
}

int ProducerTest::runPartitionerTest (const char *stream, int numParts,
                                      bool userDefinedPartitioner, int pType,
                                      int keyType) {
  rd_kafka_t *producer;
  rd_kafka_conf_t *conf;
  conf = rd_kafka_conf_new();
  bool print = false;
  char errstr[512];
  rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb_stub);
  rd_kafka_conf_set_opaque (conf, &print);
  producer = rd_kafka_new (RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));

  if (!producer) {
      cerr << "\nrunPartitionerTest: Failed to create new producer\n" <<  errstr;
      return PRODUCER_CREATE_FAILED;
  }

  rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();
  if (userDefinedPartitioner) {
    cout << "userDefinedPartitioner";
    switch (pType) {
      case 0://random partitioner
            rd_kafka_topic_conf_set_partitioner_cb (topicConf,
                                    rd_kafka_msg_partitioner_random);
            break;
      case 1://Consistent partitioner
            rd_kafka_topic_conf_set_partitioner_cb (topicConf,
                                    rd_kafka_msg_partitioner_consistent);
            break;
      case 2://Consistent random partitioner
            rd_kafka_topic_conf_set_partitioner_cb (topicConf,
                                    rd_kafka_msg_partitioner_consistent_random);
            break;
      default:
            break;
    }
  }
  char currentName[100];
  snprintf(currentName, sizeof(currentName), "%s0:topic",  stream);
  rd_kafka_topic_t *topicObj = rd_kafka_topic_new( producer, currentName, topicConf);
  bool nullKey = false;
  int err = 0;
  for (int mIdx = 0; mIdx < 100; mIdx++) {
    char sendkey[200];
    char sendvalue[200];
    size_t keySize = 0;
    switch (keyType) {
        case 0: //Null Key
                nullKey = true;
                break;
        case 1: //Same Key
                memset (sendkey, 0 , 200);
                snprintf(sendkey, sizeof(sendkey), "Key:%s0:topic", stream);
                break;
        case 2: //Diff Key
                memset (sendkey, 0 , 200);
                snprintf(sendkey, sizeof(sendkey), "Key:%s0:topic:%d", stream, mIdx);
                break;
        default:nullKey = true;
                break;
    }
    if(!nullKey)
      keySize = (size_t)strlen(sendkey);

    memset(sendvalue, 0 , 200);
    snprintf(sendvalue, sizeof(sendvalue), "Value:%s0",stream);
    size_t valSize = strlen (sendvalue);

      if (!nullKey)
        err += rd_kafka_produce(topicObj, -1, RD_KAFKA_MSG_F_COPY, sendvalue,
                                      valSize, sendkey, keySize, NULL);
      else
        err += rd_kafka_produce(topicObj, -1, RD_KAFKA_MSG_F_COPY, sendvalue,
                                      valSize, NULL, 0, NULL);
  }

  while (rd_kafka_outq_len(producer) > 0) {
    rd_kafka_poll(producer, 1);
  }
 return err;
}
