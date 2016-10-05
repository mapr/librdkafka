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
#include "../producer/Producer.cc"
#include "../producer/ProducerTest.h"
#include "../producer/ProducerTest.cc"

char *STREAM ="/gtest-ProducerTest";
uint64_t numCallbacks;
uint64_t expectedCb;

void init() {
  numCallbacks = 0;
  expectedCb = 0;
}

void msg_produce_test_case (const char* strName, int partnId,
                           bool isConfValid, bool isTopicValid,
                           char *key, char *value, int msgflag,
                           int verifyCount ) {
  if (strName) {
    ASSERT_EQ(0, stream_create(strName, 1/*Num of Streams*/,
                             1/*Default Partitions*/));
    char topicName[100];
    if (isTopicValid)
      sprintf(topicName, "%s0:topic", strName);
    else
      sprintf(topicName, "%s0topic", strName);
    ProducerTest::runProduceTest(topicName, isConfValid, key, value,
                                msgflag, partnId);
    /*Verify # of messages produced using mapr streamanalyzer*/
    EXPECT_EQ(verifyCount, stream_count_check(strName,1));
    EXPECT_EQ(SUCCESS, stream_delete(strName, 1));
  } else {
    EXPECT_EQ(PRODUCER_SEND_FAILED, ProducerTest::runProduceTest(NULL,
                                                                  isConfValid,
                                                                  key, value,
                                                                  msgflag,
                                                                  partnId));
  }
}

void combination_test (char* strName, int numStr,
                       int numTopics, int numPartition,
                       int numMsgsPerPartition, int numProducers,
                       int msgSize, bool roundRobin, int slowTopics,
                       bool printStats, uint64_t timeout){
    init();
    expectedCb = numStr * numPartition * numProducers *
                 (numTopics *numMsgsPerPartition +
                  slowTopics*numMsgsPerPartition/1000);
    ASSERT_EQ (0, stream_create (strName, numStr/*Num of Streams*/,
                                 numPartition/*Default Partitions*/));
    EXPECT_EQ (SUCCESS, ProducerTest::runProducerCombinationTest(strName,
                                        numStr, numTopics, numPartition,
                                        numMsgsPerPartition, numProducers,
                                        msgSize, roundRobin, slowTopics,
                                        printStats, timeout, &numCallbacks));
    EXPECT_EQ (expectedCb, numCallbacks);
    EXPECT_EQ (expectedCb, stream_count_check(strName, numStr));
    EXPECT_EQ (SUCCESS, stream_delete(strName, numStr));
}

/*-----------------------------------------------*/
/*Producer Create Tests*/
/*-----------------------------------------------*/
TEST(ProducerDrive, producerCreateDefaultTest) {
  EXPECT_EQ(SUCCESS, ProducerTest::runProducerCreateTest(true, true));
}
/*NULL conf is valid input*/
TEST(ProducerTest, producerCreateNullConfTest) {
  EXPECT_EQ(SUCCESS, ProducerTest::runProducerCreateTest(false, true));
}
/*NULL type is valid input, defaults to RD_KAFKA_PRODUCER*/
TEST(ProducerTest, producerCreateNullTypeTest) {
  EXPECT_EQ(SUCCESS, ProducerTest::runProducerCreateTest(true, false));
}
TEST(ProducerTest, producerCreateInvalidArgTest) {
  EXPECT_EQ(SUCCESS, ProducerTest::runProducerCreateTest(false, false));
}

/*-----------------------------------------------*/
/*Producer msg send tests*/
/*-----------------------------------------------*/
TEST(ProducerTest, msgProduceDefaultTest) {
  msg_produce_test_case (STREAM, 0, true, true, "key", "value", 0, 1);
}

TEST(ProducerTest, msgProduceMsgFlagCopyTest) {
    msg_produce_test_case (STREAM, 0, true, true, "key", "value",
                            RD_KAFKA_MSG_F_COPY,  1);
}

TEST(ProducerTest, msgProduceMsgFlagFreeTest) {
    msg_produce_test_case (STREAM, 0, true, true, "key", "value",
                            RD_KAFKA_MSG_F_FREE,  1);
}

TEST(ProducerTest, msgProduceInvalidTopicFormatTest) {
  msg_produce_test_case (STREAM, 0, true, false, "key", "value", 0, 0);
}

TEST(ProducerTest, msgProduceNullTopicTest) {
  msg_produce_test_case (NULL, 0, true, true, "key", "value", 0, 0);
}

TEST(ProducerTest, msgProduceInvalidConfTest) {
  msg_produce_test_case (STREAM, 0, false, true, "key", "value", 0, 1);
}

TEST(ProducerTest, msgProduceInvalidKeyValTest) {
  msg_produce_test_case(STREAM, 0, true, true, NULL, NULL, 0, 0);
}

TEST(ProducerTest, msgProduceInvalidPartitionTest) {
  msg_produce_test_case(STREAM, -1, true, true, "key", "value", 0, 1);
}

TEST(ProducerTest, msgProduceOutOfBoundPartitionTest) {
  msg_produce_test_case(STREAM, 5, true, true, "key", "value", 0, 0);
}

/*-----------------------------------------------*/
/*Producer mixed topic tests*/
/*-----------------------------------------------*/
TEST(ProducerTest, msgProducerMaprProducerKafkaTopicTest) {
    ASSERT_EQ(0, stream_create (STREAM, 1/*stream index*/,
                                 1/*Default Partitions*/));
    EXPECT_EQ (RD_KAFKA_RESP_ERR_TOPIC_EXCEPTION,
        ProducerTest::runProducerMixedTopicTest(STREAM, 0));
    EXPECT_EQ(0, stream_delete (STREAM, 1/*stream index*/));
}
TEST(ProducerTest, msgProducerKafkaProducerMaprTopicTest) {
    EXPECT_EQ (RD_KAFKA_RESP_ERR_TOPIC_EXCEPTION,
        ProducerTest::runProducerMixedTopicTest(STREAM, 1));
}

/*-----------------------------------------------*/
/* Single Producer msg send combination tests*/
/*-----------------------------------------------*/
TEST(ProduceeDrive, testSendOneSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1/*# of topics*/,
                    1/*# of partn per topic*/, 1/*# of msgs per partition*/,
                    1/*numProducer*/,200/*Msg size*/, true/*Round Robin*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testSendOneMediumMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1/*# of topics*/,
                    1/*# of partn per topic*/, 1/*# of msgs per partition*/,
                    1/*numProducer*/,9*1024*1024/10/*Msg size*/, true/*rr*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testSendTenTopicsMediumMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 10/*# of topics*/,
                    10/*# of partn per topic*/,
                    1000/*# of msgs per partition*/, 1/*numProducer*/,
                    9*1024*1024/10/*Msg size*/, true/*Round Robin*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
/*TODO: Fix Disabled test*/
TEST(ProducerTest, DISABLED_testSendThousandTopicsSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1000/*# of topics*/,
                    10/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                    1/*numProducer*/,200/*Msg size*/, true/*rr*/,
                    0/*Slow Topics*/, false/*Print*/,
                    60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testMixedSpeedTopics) {
  combination_test (STREAM, 1/*# of streams*/, 100/*# of topics*/,
                    1/*# of partn per topic*/, 10000/*# of msgs per partn*/,
                    1/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                    10/*Slow Topics*/, false/*Print*/,
                    60 * 1000/*pollWaitTimeOutMS*/);
}
/*----------------------------------------------*/
/* Multiple Producer msg send combination tests*/
/*-----------------------------------------------*/

TEST(ProducerTest, testTenProducerSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        10/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testTenProducerSmallMessageMultiStream) {
  combination_test (STREAM, 10/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        10/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testHundredProducerSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testHundredProducerSmallMessageMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 10/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
//TODO:System hangs CLDB becomes unavailable inbetween this test.
TEST(ProducerTest, DISABLED_testHundredProducerMixedSpeedTopicMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 10000/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        10/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testThousandProducerSmallMsgMultiStream) {
  combination_test (STREAM, 3/*# of streams*/, 4/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        1000/*numProducer*/, 200/*Msg size*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
/*----------------------------------------------*/
/* Producer Error test*/
/*-----------------------------------------------*/
//TODO: Mapping streams error to kafka errors
TEST(ProducerTest, DISABLED_msgProduceStreamDeleteTest) {
  ASSERT_EQ(SUCCESS, stream_create(STREAM, 1/*# of streams*/,
                                   1/*# of partitions*/));
  ASSERT_EQ (RD_KAFKA_RESP_ERR_TOPIC_EXCEPTION,
            ProducerTest::runProducerErrorTest(STREAM, 10000, true));
}

TEST(ProducerTest, msgProduceTopicDeleteTest) {
  ASSERT_EQ(SUCCESS, stream_create(STREAM, 1/*# of streams*/,
                                   1/*# of partitions*/));
  ASSERT_EQ (RD_KAFKA_RESP_ERR_TOPIC_EXCEPTION,
            ProducerTest::runProducerErrorTest(STREAM, 10000, false));
  EXPECT_EQ (SUCCESS, stream_delete(STREAM, 1));
}

int main (int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
