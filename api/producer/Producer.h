#ifndef SRC_PRODUCER_H_
#define SRC_PRODUCER_H_

#include "PerformanceStats.h"

class Producer {
  private:
    static char *streamName;
    static int numStreams;
    static int numTopics;
    static int numSlowTopics;
    static int numPartitions;
    static int numMsgsPerPartition;
    static int MSG_VALUE_LENGTH;
    static int MSG_KEY_LENGTH;
    static int sendflag; //RD_KAFKA_MSG_F_FREE or RD_KAFKA_MSG_F_COPY
    static bool reportMsgOffset;//true calls dr_msg_cb, false calls dr_cb
    static bool roundRobin;
    static bool printStats;
    rd_kafka_t *producer;
    uint64_t pollWaitTimeOutMS;
  public:
    struct CallbackCtx {
      int64_t start;
      int64_t bytes;
      Producer *self;
      PerformanceStats *stats;
    };

    /*Constructor*/
    Producer ();
    Producer (char* s, int nstreams, int ntopics, int npart,
              int nmsg, int msgSize, int flag, bool roundRobin,
              int slowTopics, bool print, uint64_t pollTO);
    ~Producer();
    int run(uint64_t *numCbOut);
    int runTest (char* path, int nStreams, int nTopics, int nParts,
                 int nMsgs, int msgSize, int flag, bool roundRobin, int slowTopics,
                 bool print, uint64_t pollTO, uint64_t *numCbOut);
};

void msg_delivered_dr_cb (rd_kafka_t *rk,
                          void *payload,
                          size_t len,
                          rd_kafka_resp_err_t error_code,
                          void *opaque,
                          void *msg_opaque);

void msg_delivered_dr_msg_cb (rd_kafka_t *rk,
                              const rd_kafka_message_t *rkmessage,
                              void *opaque);

Producer::Producer () {
  streamName = "";
  numStreams = 2;
  numTopics = 2;
  numSlowTopics = 0;
  numPartitions = 4;
  numMsgsPerPartition = 100000;
  sendflag = RD_KAFKA_MSG_F_COPY;
  reportMsgOffset = true; //true calls dr_msg_cb, false calls dr_cb
  MSG_VALUE_LENGTH = 200;
  MSG_KEY_LENGTH = 200;
  roundRobin = false;
  printStats = false;
  pollWaitTimeOutMS = 60*1000 ;
}

Producer::Producer (char* s, int nstreams, int ntopics, int npart,
                    int nmsg, int msgSize, int flag, bool roundRb, int slowTopics,
                    bool print, uint64_t timeout) {
  streamName = s;
  numStreams = nstreams;
  numTopics = ntopics;
  numSlowTopics = slowTopics;
  numPartitions = npart;
  numMsgsPerPartition = nmsg;
  sendflag = flag;
  reportMsgOffset = false;
  MSG_VALUE_LENGTH = msgSize;
  MSG_KEY_LENGTH = msgSize;
  roundRobin = roundRb;
  printStats = print;
  pollWaitTimeOutMS = timeout;
}

Producer::~Producer() {
}

struct retData {
  int retCode;
  uint64_t numCb;
};

struct ProducerThreadArgs {
  pthread_t thread;
  struct retData *retdata;
  Producer *producer;
};

struct ProducerPollArgs {
  pthread_t thread;
  rd_kafka_t *producer;
  uint64_t timeout;
  PerformanceStats *stats;
};

#endif
