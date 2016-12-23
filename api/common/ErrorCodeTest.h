#ifndef SRC_ERROR_CODE_H_
#define SRC_ERROR_CODE_H_

#include "../admin/Utils.h"

class ErrorCodeTest {
  public:
    static struct ErrStruct* runRdKafkaProduceApiErrorCodeTest(const char * topic,
                                    int32_t partition, int msgflags, void *payload,
                                    size_t len, const void *key, size_t keylen,
                                    void *msg_opaque, void * ctx);

    static struct ErrStruct* runSubscribeErrorCodeTest(char *strName, char *topic,
                                          bool isKafka, bool isAssign,
                                          bool addNonExistentTopic, bool commit,
                                          void *ctx);
    static int runPartitionEOFErrorCodeTest(char *strName, char *topic,
                                             bool isKafka, void *ctx);
};


typedef struct ErrStruct {
  int apiErr;
  int apiCbErr;
  int errCode;
  rd_kafka_topic_partition_list_t *assignedPartition;
  rd_kafka_topic_partition_list_t *revokedPartition;
}ErrStruct;

#endif
