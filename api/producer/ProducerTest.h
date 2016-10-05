#ifndef SRC_PRODUCER_DRIVER_H_
#define SRC_PRODUCER_DRIVER_H_

#include "../admin/Utils.h"

class ProducerTest {
  public:
    static int runProducerCreateTest (bool isConfValid, bool isTypeValid);
    static int runProduceTest (const char *topicName,
                               bool isConfValid,
                               const char *key,
                               const char *val,
                               int msgFlag,
                               int pid);
    static int runProducerCombinationTest(char *path,
                                              int nstreams, int ntopics,
                                              int nparts, int nmsgs,
                                              int nproducers, int msgsize,
                                              bool roundRb, int slowtopics,
                                              bool print, uint64_t timeout,
                                              uint64_t *numCallbacks);
    static int runProducerMixedTopicTest(char * strName, int type);
    static int runProducerErrorTest(char * strName, int numMsgs,
                                    bool streamDelete);
};
#endif

