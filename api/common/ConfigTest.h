#ifndef SRC_COMMON_DRIVER_H_
#define SRC_COMMON_DRIVER_H_

#include "../admin/Utils.h"

class ConfigTest {
  public:
          static int runProducerConfigTest (char *str, char *topic,
                                                 char *pDefaultStr, int numMsgs,
                                                 int maxMessageSize);
          static int runConsumerRecvMaxMsgSizeConfigTest (char *str, char *topic,
                                             int numMsgs, int msgSize,
                                             int recvMaxMsgSize, bool mixSizes );
};
#endif
