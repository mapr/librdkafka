#ifndef SRC_PRODUCER_DRIVER_H_
#define SRC_PRODUCER_DRIVER_H_

#include "../admin/Utils.h"

class ConsumerTest {
  public:
    static int runConsumerCreateTest(bool isConfValid, bool isTypeValid);
    static int runSubscribeTest(char *strName, int numStreams, int numTopics,
                                int nParts, bool isConsumerValid, int consumerType,
                                int topicType, const char *group, bool isAssign,
                                char *cDefaultStr);
    static uint64_t runPollTest(char *path, int nstreams, int ntopics,int nparts,
                              int nmsgs, int msgsize, int flag,
                              bool roundRb, int nslowtopics, bool print,
                              uint64_t timeout, const char* groupid, bool topicSub,
                              bool autoCommit, bool verify);
    static int runUnsubscribeTest (char *path, int nstreams, int ntopics,
                                    int nparts, int nmsgs,
                                    int msgsize, bool roundRb, int nslowtopics,
                                    bool print, uint64_t timeout,
                                    const char* groupid);
    static void runAssignRevokeCbTest(char *strName, int nparts);
    static int runCommitTest (char * strName, const char *groupid,
                              bool consumerInvalid,
                              bool topicInvalid, bool offsetInvalid);
    static int runConsumerCloseTest (char *strName, char * groupid,
                                            bool consumerInvalid);
    static int runConsumerBack2BackTest (char *strName);
    static void runConsumerSeekPositionTest (char *strName, char * groupid,
                                            bool print);
    static rd_kafka_resp_err_t runRegexTest(char *str1, char *str2, int type,
                                            bool isBlackList);
    static rd_kafka_resp_err_t runConsumerListTest (char *strName,
                                   const char *groupid,
                                   const struct rd_kafka_group_list ** glist);
    static rd_kafka_resp_err_t runUnassignTest (char *strName, int nTopics, int npart);
};

struct RebalanceCbCtx {
  int subscriptionCnt;
  int unsubscribeCnt;
  rd_kafka_topic_partition_list_t *subscribedList;
  int id;
};

struct ConsumerThreadArgs {
  pthread_t thread;
  int id;
  char *path;
  int *topicIds; //array of topic ids
  int minPartId;
  int maxPartId;
  bool killSelf;
  char * group;
  int numMsgs;
  struct RebalanceCbCtx *ctx;
};

#endif

