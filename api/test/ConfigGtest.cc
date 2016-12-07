/**
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements. See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership. The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License. You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <gtest/gtest.h>
#include "../../src/rdkafka.h"
#include "../common/ConfigTest.h"
#include "../common/ConfigTest.cc"
#include "../consumer/Consumer.cc"
#include "../producer/Producer.cc"
#include "../consumer/ConsumerTest.h"
#include "../consumer/ConsumerTest.cc"
char *STREAM_CONFIG = "/gtest-config";
char *STREAM_PRODUCER_DEFAULT = "/gtest-ProducerDefault";

class consumerDefaultStrConfigTest: public testing::Test {

protected:
  char *strName ;
  char *defaultStrName ;
  virtual void SetUp() {
    strName = "/gtest-ConfigTest";
    stream_create(strName, 2/*Num of Streams*/, 4/*Default Partitions*/);
    sleep (1);
    defaultStrName = "/gtest-ConsumerDefault";
    stream_create(defaultStrName, 1/*Num of Streams*/, 1/*Default Partitions*/);
  }
  virtual void TearDown() {
    stream_delete(strName, 2);
    stream_delete(defaultStrName, 1);
  }
};

void producer_config_test (char *str, char *topicName,
                              char *pStr, int numMsgs, int maxMsgSize,
                              char *verifyStr, int verifyCount,
                              int err_expected) {

  if (str)
    ASSERT_EQ(0, stream_create(str, 1/*Num of Streams*/, 1/*Default Partitions*/));
  if (pStr)
    ASSERT_EQ(0, stream_create(pStr, 1/*Num of Streams*/, 1/*Default Partitions*/));

    //test producer default stream config
    int err = ConfigTest::runProducerConfigTest (str,
                            topicName, pStr, numMsgs, maxMsgSize);
    if (verifyStr)
      EXPECT_EQ (verifyCount, stream_count_check(verifyStr,1));

  EXPECT_EQ (err_expected, err);
  if (str)  EXPECT_EQ(SUCCESS, stream_delete(str, 1));
  if (pStr) EXPECT_EQ(SUCCESS, stream_delete(pStr, 1));
}

void consumer_recv_config_test (char *str, char *topicName, int numMsgs,
                                int msgSize, int recvMaxMsgSize, bool mixSizes,
                                int verifyErrCount) {

  if (str)
    ASSERT_EQ(SUCCESS, stream_create(str, 1/*Num of Streams*/, 1/*Default Partitions*/));
  sleep (1);
  //test producer default stream config
  int err = ConfigTest::runConsumerRecvMaxMsgSizeConfigTest (str, topicName,
                                            numMsgs, msgSize, recvMaxMsgSize,
                                            mixSizes);
  EXPECT_EQ (verifyErrCount, err);

  if (str)  EXPECT_EQ(SUCCESS, stream_delete(str, 1));
}
/*-----------------------------------------------*/
/* Producer default stream name config Tests*/
/*-----------------------------------------------*/

TEST(ConfigTest, noDefaultStreamProduceTest) {
    producer_config_test (STREAM_CONFIG, "topic0", NULL, 100, -1,
                             STREAM_CONFIG, 100, RD_KAFKA_RESP_ERR_NO_ERROR);
}
TEST(ConfigTest, producerDefaultStreamProduceTest) {
    producer_config_test (NULL, "topic0",STREAM_PRODUCER_DEFAULT,
                             500, -1, STREAM_PRODUCER_DEFAULT, 500,
                             RD_KAFKA_RESP_ERR_NO_ERROR);
}
TEST(ConfigTest, producerDefaultStreamInvalidTopicProduceTest) {
    producer_config_test (NULL, "topic:topic",STREAM_PRODUCER_DEFAULT,
                             500, -1, STREAM_PRODUCER_DEFAULT, 0,
                             RD_KAFKA_RESP_ERR__INVALID_ARG);
}
TEST(ConfigTest, noDefaultStreamInvalidTopicProduceTest) {
    producer_config_test (NULL, "topic:topic", 0,
                             500, -1, STREAM_PRODUCER_DEFAULT, 0,
                             RD_KAFKA_RESP_ERR_NO_ERROR);
}
TEST(ConfigTest, regularStreamWithproducerDefaultStreamProduceTest) {
    producer_config_test (STREAM_CONFIG, "topic0",STREAM_PRODUCER_DEFAULT,
                             700, -1, STREAM_CONFIG, 700,
                             RD_KAFKA_RESP_ERR_NO_ERROR);
}
TEST(ConfigTest, defaultMaxMesageSizeConfProduceTest) {
      producer_config_test (STREAM_CONFIG, "topic0", NULL, 1500, 1000000,
                            STREAM_CONFIG, 0, RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE);
}
TEST(ConfigTest, lowerLimitMaxMessageSizeConfProduceTest) {
      producer_config_test (STREAM_CONFIG, "topic0", NULL, 1500, 500,
                            STREAM_CONFIG, 1500, RD_KAFKA_RESP_ERR_NO_ERROR);
}
TEST(ConfigTest, upperLimitMaxMesageSizeConfProduceTest) {
      producer_config_test (STREAM_CONFIG, "topic0", NULL, 5, 1000000002,
                            STREAM_CONFIG, 0, RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE);
}

/*-----------------------------------------------*/
/* Consumer default stream name config subscribe/subscription tests*/
/*-----------------------------------------------*/

TEST_F(consumerDefaultStrConfigTest, maprConsumerDefaultStrConfigTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 1, 1, 1, true,
                                                     0, 0, "ConsumerTest", false,
                                                    defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerDefaultStrConfigTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                                     0, 1, "ConsumerTest", false,
                                                    defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerInvalidTypeSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 0, "ConsumerTest", false,
                                      defaultStrName));

}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerInvalidTypeSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 1, "ConsumerTest", false,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerInvalidGroupSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 0, NULL, false,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerInvalidGroupSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 1, NULL, false,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerMaprTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 2, 2, 4, true,
                                                     1, 0, "ConsumerTest", false,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerKafkaTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                      1, 1, "ConsumerTest", false,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerMixedTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     1, 2, "ConsumerTest", false,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerKafkaTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 1, "ConsumerTest", false,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerMaprTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 0, "ConsumerTest", false,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerMixedTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 2, "ConsumerTest", false,
                                                     defaultStrName));
}

/*-----------------------------------------------*/
/*Consumer default stream config assign/assignment tests*/
/*-----------------------------------------------*/
TEST_F(consumerDefaultStrConfigTest, maprConsumerDefaultAssignTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 1, 1, 1, true,
                                                     0, 0, "ConsumerTest", true,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerDefaultAssignTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                                     0, 1, "ConsumerTest", true,
                                                     defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerInvalidTypeAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 0, "ConsumerTest", true,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerInvalidTypeAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 1, "ConsumerTest", true,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerInvalidGroupAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 0, NULL, true,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, kafkaConsumerInvalidGroupAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 1, NULL, true,
                                      defaultStrName));
}
TEST_F(consumerDefaultStrConfigTest, maprConsumerMaprTopicAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR_ILLEGAL_GENERATION, ConsumerTest::runSubscribeTest (strName, 2, 2, 4, true,
                                                     1, 0, "ConsumerTest", true,
                                                     defaultStrName));
}


/*-----------------------------------------------*/
/* Consumer recv max message size config Tests*/
/*-----------------------------------------------*/


TEST(ConfigTest, consumerRecvMaxMsgSizeGreaterThanProducedMsgSizeConfigTest) {
 consumer_recv_config_test (STREAM_CONFIG, "topic0", 10000, 10000, 15000, false, 0);
}
TEST(ConfigTest, consumerRecvMaxMsgSizeLessThanProducedMsgSizeConfigTest) {
 consumer_recv_config_test (STREAM_CONFIG, "topic0", 5000, 10000, 1500, false, 5000);
}
TEST(ConfigTest, consumerRecvMixedMsgSizeConfigTest) {
 consumer_recv_config_test (STREAM_CONFIG, "topic0", 10000, 10000, 1500, true, 5000);
}

int main (int argc, char **argv) {
    testing::InitGoogleTest (&argc, argv);
      return RUN_ALL_TESTS();
}
