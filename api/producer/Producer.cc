#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include <sys/time.h>
#include "Producer.h"
#include "PerformanceStats.cc"
using namespace std;

char* Producer::streamName;
int Producer::numStreams;
int Producer::numTopics;
int Producer::numSlowTopics;
int Producer::numPartitions;
int Producer::numMsgsPerPartition;
int Producer::sendflag; //RD_KAFKA_MSG_F_FREE or RD_KAFKA_MSG_F_COPY
bool Producer::reportMsgOffset;//true calls dr_msg_cb, false calls dr_cb
bool Producer::roundRobin;
bool Producer::printStats;
int Producer::MSG_KEY_LENGTH;
int Producer::MSG_VALUE_LENGTH;

void msg_delivered_dr_cb (rd_kafka_t *rk,
                          void *payload, size_t len,
                          rd_kafka_resp_err_t  error_code,
                          void *opaque,
                          void *msg_opaque) {
	Producer::CallbackCtx *myCtx = (Producer::CallbackCtx *) msg_opaque;
  uint64_t now = 0;
	if (myCtx->start)
		now = CurrentTimeMillis();

  myCtx->stats->report(now - myCtx->start, myCtx->bytes);
  delete myCtx;
};
void msg_delivered_dr_msg_cb (rd_kafka_t *rk,
                              const rd_kafka_message_t *rkmessage,
                              void *opaque) {
	Producer::CallbackCtx *myCtx = (Producer::CallbackCtx *) rkmessage->_private;
  uint64_t now = 0;
	if (myCtx->start)
		now = CurrentTimeMillis();

  myCtx->stats->report(now - myCtx->start, myCtx->bytes);
  delete myCtx;

};

void *ProducerPoll(void *arg) {
	ProducerPollArgs *pt = static_cast<ProducerPollArgs *> (arg);
	int *ret = (int *) malloc(sizeof(int));
  uint64_t pollStart = CurrentTimeMillis();
  uint64_t volatile pollCur = pollStart;
  while (pt->stats->run && ((pollCur - pollStart) < pt->timeout))
	{
      rd_kafka_poll(pt->producer, 100);
      pollCur = CurrentTimeMillis();
	}
  if(!(pt->stats->run) && ((pollCur - pollStart) > pt->timeout))
    *ret = -1 ;
  else
    *ret = 0;

  return (void *)ret;
}

int Producer::run(uint64_t *numCb) {

	if ( strcmp(streamName,"") == 0 || strlen(streamName)==0) {
		cerr << "Stream name can't be empty. "<< "\n";
		return INVALID_ARGUMENT;
	}
	if ( numPartitions <= 0) {
		cerr << "num of partitions cannot be negative or zero." << "\n";
		return INVALID_ARGUMENT;
	}
	if ( numTopics <= 0) {
		cerr << "num of topicss cannot be negative or zero." << "\n";
		return INVALID_ARGUMENT;
	}

	int totalNumTopics = numTopics + numSlowTopics;
	int totalNumMsgs =  numMsgsPerPartition *  numPartitions *
                      numTopics *  numStreams;
	totalNumMsgs += numMsgsPerPartition/1000 * numPartitions *
                  numSlowTopics * numStreams;

	rd_kafka_topic_t **topicArr = (rd_kafka_topic_t **) malloc (numStreams *
                                  totalNumTopics* sizeof(rd_kafka_topic_t *));

	rd_kafka_conf_t *conf = rd_kafka_conf_new();
	rd_kafka_topic_conf_t *topicConf = rd_kafka_topic_conf_new();

	if (reportMsgOffset) {
		rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_dr_msg_cb);
	}	else {
		rd_kafka_conf_set_dr_cb(conf, msg_delivered_dr_cb);
	}
	//create rd_kafka_new (producer) object
	char *errstr = NULL;
	if (!(producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                                errstr, sizeof(errstr)))) {
		cout << " Failed to create new producer\n" <<  errstr;
    for (int s = 0; s <  numStreams * totalNumTopics; ++s)
           rd_kafka_topic_destroy(topicArr[s]);
		return PRODUCER_CREATE_FAILED;
  }

	//Populate topics to send messages to.
	for (int s = 0; s <  numStreams; ++s) {
     for (int i = 0; i <  totalNumTopics; ++i) {
			char currentName[100];
      int tempIndex = s* totalNumTopics +i;
			sprintf(currentName, "%s%d:topic%d",  streamName, s, i);
			topicArr[tempIndex] = rd_kafka_topic_new( producer, currentName,
                                        rd_kafka_topic_conf_dup(topicConf));
			}
	}

  PerformanceStats *statscollector = new PerformanceStats(totalNumMsgs);
  ProducerPollArgs pollArgs;
	pollArgs.producer = producer;
  pollArgs.stats = statscollector;
	pollArgs.timeout = pollWaitTimeOutMS;
  pthread_create(&pollArgs.thread, NULL /*attr*/, ProducerPoll, &pollArgs);
	int err = SUCCESS;
  char **sendkeyArr = NULL;
  char **sendvalueArr = NULL;

  if (sendflag & RD_KAFKA_MSG_F_FREE) {
      sendkeyArr = (char **) malloc (totalNumMsgs * sizeof(char *));
      sendvalueArr = (char **) malloc (totalNumMsgs * sizeof(char *));
  }

  //Populate messages and send them.
  for (int sIdx = 0; sIdx <  numStreams; sIdx++) {
    for (int tIdx = 0; tIdx < totalNumTopics; tIdx++) {
      rd_kafka_topic_t *topicObj = topicArr[sIdx * totalNumTopics + tIdx];
      for (int pIdx = 0; pIdx <  numPartitions; pIdx++) {
        for (int mIdx = 0; mIdx <  numMsgsPerPartition; mIdx++) {
          if(tIdx >=numTopics) {
            if(mIdx % 1000 != 0)
              continue;
          }
          //populate key and message to be produced.
          if (sendflag & RD_KAFKA_MSG_F_FREE) {
            int index = sIdx * totalNumTopics * numPartitions * numMsgsPerPartition +
                          tIdx * numPartitions * numMsgsPerPartition +
                            pIdx * numMsgsPerPartition + numMsgsPerPartition;
            sendkeyArr[index] = (char *) malloc (MSG_KEY_LENGTH * sizeof(char));
            sendvalueArr[index] = (char *) malloc (MSG_VALUE_LENGTH * sizeof(char));
            memset (sendkeyArr[index], '\0', MSG_KEY_LENGTH);
            sprintf(sendkeyArr[index], "Key:%s%d:topic%d:%d:%d:%d:%d",streamName,
                                          sIdx, tIdx,sIdx, tIdx, pIdx, mIdx);
            size_t ksize = (size_t)strlen(sendkeyArr[index]);
            memset(sendvalueArr[index], 'a' , MSG_VALUE_LENGTH);
            sprintf(sendvalueArr[index], "Value:%s%d:%d:%d:%d",streamName,
                                          sIdx, tIdx, pIdx, mIdx);
            size_t vsize = MSG_VALUE_LENGTH;
            uint64_t sendTime =  CurrentTimeMillis();
            CallbackCtx *ctx = new CallbackCtx;
            ctx->start = sendTime;
            ctx->bytes =  MSG_KEY_LENGTH + vsize;
            statscollector->run = true;
            ctx->stats = statscollector;
            ctx->self = this;
            if(roundRobin)
              err += rd_kafka_produce(topicObj, pIdx, sendflag, sendvalueArr[index],
                                      vsize, NULL, 0, (void *) ctx);
            else
              err += rd_kafka_produce(topicObj, pIdx, sendflag, sendvalueArr[index],
                                      vsize, sendkeyArr[index], ksize, (void *) ctx); 
          } else {
            char sendkey[MSG_KEY_LENGTH];
            memset (sendkey, '\0', MSG_KEY_LENGTH);
            sprintf(sendkey, "Key:%s%d:topic%d:%d:%d:%d:%d",streamName,
                                            sIdx, tIdx,sIdx, tIdx, pIdx, mIdx);
            size_t keySize = (size_t)strlen(sendkey);
            char sendvalue[ MSG_VALUE_LENGTH];
            memset(sendvalue, 'a' , MSG_VALUE_LENGTH);
            sprintf(sendvalue, "Value:%s%d:%d:%d:%d",streamName,
                                            sIdx, tIdx, pIdx, mIdx);
            size_t valSize = MSG_VALUE_LENGTH;
            uint64_t sendTime =  CurrentTimeMillis();

            //create callback context
            CallbackCtx *ctx = new CallbackCtx;
            ctx->start = sendTime;
            ctx->bytes =  MSG_KEY_LENGTH + valSize;
            statscollector->run = true;
            ctx->stats = statscollector;
            ctx->self = this;
            if(roundRobin)
              err += rd_kafka_produce(topicObj, pIdx, sendflag, sendvalue,
                                      valSize, NULL, 0, (void *) ctx);
            else
              err += rd_kafka_produce(topicObj, pIdx, sendflag, sendvalue,
                                      valSize, sendkey, keySize, (void *) ctx);
          }
        }
      }
    }
  }

  void* retVal;
  int tempRet = 0;
  //Wait for Poll thread to join
  pthread_join(pollArgs.thread, &retVal);

  if (*(int *)retVal!=0) {
    cerr << "\nPoll timedout after " << pollWaitTimeOutMS/1000 << " seconds";
    tempRet = PRODUCER_POLL_FAILED;
  }
  else {
    if(err != SUCCESS)
      tempRet = PRODUCER_SEND_FAILED;
    else
      tempRet = SUCCESS;
  }

  free(retVal);
  //Verify produced message count.
  statscollector->checkAndVerify(printStats, numCb);

  if(printStats)
    statscollector->printReport(totalNumTopics, numPartitions);

  for (int s = 0; s <  numStreams * totalNumTopics; ++s)
    rd_kafka_topic_destroy(topicArr[s]);

  rd_kafka_destroy(producer);
  return tempRet;
}

int Producer::runTest(char *path, int nStreams, int nTopics, int nParts,
                      int nMsgs, int msgSize, int flag, bool roundRb,
                      int slowTopics, bool print, uint64_t timeout,
                      uint64_t *nCallBacks) {
  streamName = path;
  numStreams = nStreams;
  numTopics = nTopics;
  numSlowTopics = slowTopics;
  numPartitions = nParts;
  numMsgsPerPartition = nMsgs;
  sendflag = flag;
  reportMsgOffset = false;
  MSG_VALUE_LENGTH = msgSize;
  MSG_KEY_LENGTH = 200;
  roundRobin = roundRb;
  printStats = print;
  pollWaitTimeOutMS = timeout;
  return run (nCallBacks);
}
