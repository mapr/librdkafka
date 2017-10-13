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
char *STREAM_BATCH ="/gtest-BatchProduceTest";
const char *STREAM_PARTITIONER = "/gtest-partitioner";

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
      snprintf(topicName, sizeof (topicName),
          "%s0:topic", strName);
    else
      snprintf(topicName, sizeof (topicName),
          "%s0topic", strName);
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
                       int msgSize, int flag, bool roundRobin, int slowTopics,
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
                                        msgSize, flag, roundRobin, slowTopics,
                                        printStats, timeout, &numCallbacks));
    EXPECT_EQ (expectedCb, stream_count_check(strName, numStr));
    EXPECT_EQ (expectedCb, numCallbacks);
    EXPECT_EQ (SUCCESS, stream_delete(strName, numStr));
}

void produce_batch_test (char* strName, char *topicName, int startPartId,
                         int numPartition, int flag, int totalMsgs){
    ASSERT_EQ (0, stream_create (strName, 1/*Num of Streams*/,
                                 numPartition/*Default Partitions*/));
    EXPECT_EQ (totalMsgs, ProducerTest::runProducerBatchTest(strName,
                                        topicName, startPartId, numPartition,
                                        flag, totalMsgs));
    EXPECT_EQ (totalMsgs, stream_count_check(strName, 1));
    EXPECT_EQ (SUCCESS, stream_delete(strName, 1));
}

void produce_mix_topic_test (char*strName, int type, int flag) {

    ASSERT_EQ(0, stream_create (strName, 1/*stream index*/,
                                 1/*Default Partitions*/));
    EXPECT_EQ (0, ProducerTest::runProducerMixedTopicTest(strName, type, flag));
    EXPECT_EQ(0, stream_delete (strName, 1/*stream index*/));
}

void producer_outq_len_test (char*strName, int numStreams, int numTopics,
                             int numParts, int numMsgsPerParts,
                             bool isCbConfigured, bool poll, uint64_t timeout) {

    ASSERT_EQ(0, stream_create (strName, numStreams/*stream index*/,
                                 numParts/*Default Partitions*/));
    int output = ProducerTest::runProduceOutqLenTest (strName,
                                                      numStreams/*# of streams*/,
                                                      numTopics/*# of topics*/,
                                                      numParts/*# of partitions*/,
                                                      numMsgsPerParts/*msgs/partn*/,
                                                      isCbConfigured/*configure cb*/,
                                                      poll/*Poll*/,
                                                      timeout/*Timeout*/);
    if (!poll && isCbConfigured)
      EXPECT_EQ (TEST_TIMED_OUT, output);
    else
      EXPECT_EQ (SUCCESS, output);

    int totalMsgs = numStreams * numTopics * numParts * numMsgsPerParts;
    EXPECT_EQ (totalMsgs, stream_count_check(strName, numStreams));
    EXPECT_EQ(0, stream_delete (strName, numStreams/*stream index*/));
}

void run_partitioner_test (const char *strName, int numPart,
                           bool userDefinedPartitioner, int partitionerType,
                           int keyType){
  ASSERT_EQ(0, stream_create(strName, 1/*Num of Streams*/,
                             numPart/*Default Partitions*/));
  ProducerTest::runPartitionerTest (strName, numPart, userDefinedPartitioner,
                                    partitionerType, keyType);
  /*TODO: Add Verify step*/
}


/*-----------------------------------------------*/
/*Producer Create Tests*/
/*-----------------------------------------------*/
TEST(ProducerTest, producerCreateDefaultTest) {
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
    char *value = (char *)malloc(6 * sizeof(char));
    strcpy(value, "Value");
    msg_produce_test_case (STREAM, 0, true, true, "Key", value,
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
  /*Allow null key-value as of mapr v6.0.0*/
  msg_produce_test_case(STREAM, 0, true, true, NULL, NULL, 0, 1/*verifyCount*/);
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
  produce_mix_topic_test (STREAM, 0, RD_KAFKA_MSG_F_COPY);
}
TEST(ProducerTest, msgProducerKafkaProducerMaprTopicTest) {
  produce_mix_topic_test (STREAM, 1, RD_KAFKA_MSG_F_COPY);
}
TEST(ProducerTest, msgProducerMaprProducerKafkaTopicMsgFreeTest) {
  produce_mix_topic_test (STREAM, 0, RD_KAFKA_MSG_F_FREE);
}
TEST(ProducerTest, msgProducerKafkaProducerMaprTopicMsgFreeTest) {
  produce_mix_topic_test (STREAM, 1, RD_KAFKA_MSG_F_FREE);
}

/*-----------------------------------------------*/
/* Single Producer msg send combination tests*/
/*-----------------------------------------------*/
TEST(ProducerTest, testSendOneSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1/*# of topics*/,
                    1/*# of partn per topic*/, 1/*# of msgs per partition*/,
                    1/*numProducer*/,200/*Msg size*/, RD_KAFKA_MSG_F_COPY/*flag*/,
                    true/*Round Robin*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testSendOneMediumMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1/*# of topics*/,
                    1/*# of partn per topic*/, 1/*# of msgs per partition*/,
                    1/*numProducer*/,9*1024*1024/10/*Msg size*/,
                    RD_KAFKA_MSG_F_COPY/*flag*/, true/*rr*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testSendTenTopicsMediumMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 10/*# of topics*/,
                    2/*# of partn per topic*/,
                    10/*# of msgs per partition*/, 1/*numProducer*/,
                    9*1024*1024/10/*Msg size*/,
                    RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testSendTenTopicsMediumMessageMsgFreeSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 10/*# of topics*/,
                    2/*# of partn per topic*/,
                    10/*# of msgs per partition*/, 1/*numProducer*/,
                    9*1024*1024/10/*Msg size*/,
                    RD_KAFKA_MSG_F_FREE/*flag*/, true/*Round Robin*/,
                    0/*Slow Topics*/, false/*Print*/,
                    45 * 1000/*pollWaitTimeOutMS*/);
}
/*TODO: Fix Disabled test*/
TEST(ProducerTest, DISABLED_testSendThousandTopicsSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 1000/*# of topics*/,
                    10/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                    1/*numProducer*/,200/*Msg size*/,
                    RD_KAFKA_MSG_F_COPY/*flag*/, true/*rr*/,
                    0/*Slow Topics*/, false/*Print*/,
                    60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testMixedSpeedTopics) {
  combination_test (STREAM, 1/*# of streams*/, 100/*# of topics*/,
                    1/*# of partn per topic*/, 10000/*# of msgs per partn*/,
                    1/*numProducer*/, 200/*Msg size*/,
                    RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                    10/*Slow Topics*/, false/*Print*/,
                    60 * 1000/*pollWaitTimeOutMS*/);
}
/*----------------------------------------------*/
/* Multiple Producer msg send combination tests*/
/*-----------------------------------------------*/

TEST(ProducerTest, testTenProducerSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        10/*numProducer*/, 200/*Msg size*/, RD_KAFKA_MSG_F_COPY/*flag*/,
                        true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testTenProducerSmallMessageMultiStream) {
  combination_test (STREAM, 10/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        10/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testHundredProducerSmallMessageSingleStream) {
  combination_test (STREAM, 1/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 1000/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}

TEST(ProducerTest, testHundredProducerSmallMessageMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 10/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testHundredProducerSmallMessageMsgFreeMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 10/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_FREE/*flag*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, testHundredProducerSmallMessageMsgCopyMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 10/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_COPY/*flag*/, true/*Round Robin*/,
                        0/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
//TODO:System hangs CLDB becomes unavailable inbetween this test.
TEST(ProducerTest, DISABLED_testHundredProducerMixedSpeedTopicMultiStream) {
  combination_test (STREAM, 5/*# of streams*/, 2/*# of topics*/,
                        5/*# of partn per topic*/, 10000/*# of msgs per partn*/,
                        100/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_FREE/*flag*/, true/*Round Robin*/,
                        10/*Slow Topics*/, false/*Print*/,
                        60 * 1000/*pollWaitTimeOutMS*/);
}
TEST(ProducerTest, DISABLED_testThousandProducerSmallMsgMultiStream) {
  combination_test (STREAM, 3/*# of streams*/, 4/*# of topics*/,
                        5/*# of partn per topic*/, 100/*# of msgs per partn*/,
                        1000/*numProducer*/, 200/*Msg size*/,
                        RD_KAFKA_MSG_F_FREE/*flag*/, true/*Round Robin*/,
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

TEST(ProducerTest, DISABLED_msgProduceTopicDeleteTest) {
  ASSERT_EQ(SUCCESS, stream_create(STREAM, 1/*# of streams*/,
                                   1/*# of partitions*/));
  ASSERT_EQ (RD_KAFKA_RESP_ERR_TOPIC_EXCEPTION,
            ProducerTest::runProducerErrorTest(STREAM, 10000, false));
  EXPECT_EQ (SUCCESS, stream_delete(STREAM, 1));
}
/*----------------------------------------------*/
/* Produce batch test*/
/*-----------------------------------------------*/
TEST (ProducerTest, produceBatchDefaultTest) {
  produce_batch_test (STREAM_BATCH, "topic"/*topic name*/, 0/*start partition id*/,
   4/* num of partitions*/, RD_KAFKA_MSG_F_COPY/*flag*/, 10000/*# of total msgs*/);
}

TEST (ProducerTest, produceBatchMsgFreeTest) {
  produce_batch_test (STREAM_BATCH, "topic"/*topic name*/, 0/*start partition id*/,
   4/* num of partitions*/, RD_KAFKA_MSG_F_FREE/*flag*/, 10000/*# of total msgs*/);
}

TEST (ProducerTest, produceBatchUnknownPartitionMsgCopyTest) {
  produce_batch_test (STREAM_BATCH, "topic"/*topic name*/, -1/*start partition id*/,
   4/* num of partitions*/, RD_KAFKA_MSG_F_COPY/*flag*/, 10000/*# of total msgs*/);
}

TEST (ProducerTest, produceBatchUnknownPartitionMsgFreeTest) {
  produce_batch_test (STREAM_BATCH, "topic"/*topic name*/, -1/*start partition id*/,
   4/* num of partitions*/, RD_KAFKA_MSG_F_FREE/*flag*/, 10000/*# of total msgs*/);
}

/*----------------------------------------------*/
/* Producer outq len test*/
/*-----------------------------------------------*/
TEST(ProducerTest, outqLenDefaultTest) {
  producer_outq_len_test(STREAM, 1/*# os streams*/, 2/*# of topics*/,
                         4/*# of partitions*/, 100000/*msgs/partn*/,
                         true/*cb configured*/, true/*poll*/, 30*1000/*Timeout*/);
}

TEST(ProducerTest, outqLenCbConfigureNoPollTest) {
  producer_outq_len_test(STREAM, 1/*# os streams*/, 2/*# of topics*/,
                         4/*# of partitions*/, 10000/*msgs/partn*/,
                         true/*cb configured*/, false/*Poll*/, 30*1000/*Timeout*/);
}

TEST(ProducerTest, outqLenNoCbConfigurePollTest) {
  producer_outq_len_test(STREAM, 1/*# os streams*/, 2/*# of topics*/,
                         4/*# of partitions*/, 10000/*msgs/partn*/,
                         false/*cb configured*/, true/*Poll*/, 30*1000/*Timeout*/);
}

TEST(ProducerTest, utqLenNoCbConfigureNoPollTest) {
  producer_outq_len_test(STREAM, 1/*# os streams*/, 2/*# of topics*/,
                         4/*# of partitions*/, 10000/*msgs/partn*/,
                         false/*cb configured*/, false/*Poll*/, 30*1000/*Timeout*/);
}

/*----------------------------------------------*/
/* Producer partitioner test*/
/*-----------------------------------------------*/

TEST (ProducerTest, partitionerNullKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, false,
                        -1/*partitioner not defined*/, 0/*Null key*/);
}

TEST (ProducerTest, partitionerSameKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, false,
                        -1/*partitioner not defined*/, 1/*Same key*/);
}

TEST (ProducerTest, partitionerDiffKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, false,
                        -1/*partitioner not defined*/, 2/*Diff key*/);
}

TEST (ProducerTest, userRandomPartitionerNullKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 0/*Random partitioner*/,
                        0/*Null key*/);
}

TEST (ProducerTest, userRandomPartitionerSameKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 0/*Random partitioner*/,
                        1/*Same key*/);
}

TEST (ProducerTest, userRandomPartitionerDiffKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 0/*Random partitioner*/,
                        2/*Different key*/);
}

TEST (ProducerTest, userConsistentPartitionerNullKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 1/*Consistent partitioner*/,
                        0/*Null key*/);
}

TEST (ProducerTest, userConsistentPartitionerSameKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 1/*Consistent partitioner*/,
                        1/*Same key*/);
}

TEST (ProducerTest, userConsistentPartitionerDiffKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true, 1/*Consistent partitioner*/,
                        2/*Different key*/);
}
TEST (ProducerTest, userConsistentRandomPartitionerNullKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true,
                        2/*Consistent-Random partitioner*/, 0/*Null key*/);
}

TEST (ProducerTest, userConsistentRandomPartitionerSameKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true,
                        2/*Consistent-Random partitioner*/, 1/*Same key*/);
}

TEST (ProducerTest, userConsistentRandomPartitionerDiffKeyTest) {
  run_partitioner_test (STREAM_PARTITIONER, 4, true,
                        2/*Consistent-Random partitioner*/, 2/* Diff key*/);
}

int main (int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
