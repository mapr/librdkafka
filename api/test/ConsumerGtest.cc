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
#include "../consumer/Consumer.cc"
#include "../producer/Producer.cc"
#include "../consumer/ConsumerTest.h"
#include "../consumer/ConsumerTest.cc"

char *STREAM_COMMIT = "/gtest-ConsumerCommit";
char *STREAM_POLL = "/gtest-ConsumerPoll";
char *STREAM_UNSUBSCRIBE = "/gtest-ConsumerUnsubscribe";
char *STREAM_CLOSE = "/gtest-ConsumerClose";
char *STREAM_COMBINATION = "/gtest-ConsumerCombination";
char *STREAM_CONSUMER_GR_LIST = "/gtest-consumerGrListTest";
char *STREAM_UNASSIGN = "/gtest-ConsumerNullAssignTest";

class SubscribeTest: public testing::Test {

protected:
  char *strName ;
  virtual void SetUp() {
    strName = "/gtest-ConsumerTest";
    stream_create(strName, 2/*Num of Streams*/, 4/*Default Partitions*/);
    sleep (1);
  }
  virtual void TearDown() {
    stream_delete(strName, 2);
  }
};

class RegexTest: public testing::Test {

protected:
  char *strName1 ;
  char *strName2 ;
  virtual void SetUp() {
    strName1 = "/gtest-ConsumerRegexTest1";
    strName2 = "/gtest-ConsumerRegexTest2";
    stream_create(strName1, 1/*Num of Streams*/, 1/*Default Partitions*/);
    sleep (1);
    stream_create(strName2, 1/*Num of Streams*/, 1/*Default Partitions*/);
    sleep (1);
  }
  virtual void TearDown() {
    stream_delete(strName1, 1);
    stream_delete(strName2, 1);
  }
};

void consumer_poll_test_case (char *path, int nstreams, int ntopics,int nparts,
                              int nmsgs, int msgsize, int flag,
                              bool roundRb, int nslowtopics, bool print,
                              uint64_t timeout, const char* groupid, bool topicSub,
                              bool autoCommit, bool verify) {
  uint64_t expectedMsgs =  nstreams * nparts *
                  (ntopics * nmsgs + nslowtopics * nmsgs /1000);
  ASSERT_EQ (0, stream_create (path, nstreams , nparts));
  uint64_t actualMsg = ConsumerTest::runPollTest (path, nstreams, ntopics,
                                            nparts, nmsgs, msgsize,flag,
                                            roundRb, nslowtopics, print, timeout,
                                            groupid, topicSub, autoCommit, verify );
  EXPECT_EQ (expectedMsgs, actualMsg);
  ASSERT_EQ (0, stream_delete (path, nstreams));
}

void consumer_offset_commit_test_case (char *strName, const char *groupid,
                                       bool consumerInvalid, bool topicInvalid,
                                       bool offsetInvalid) {
  ASSERT_EQ(0, stream_create(strName, 1, 1));
  sleep (1);
  int out = ConsumerTest::runCommitTest (strName, groupid , consumerInvalid,
                                        topicInvalid, offsetInvalid);
  int err = 0;
  if(topicInvalid || consumerInvalid)
    err = RD_KAFKA_RESP_ERR__INVALID_ARG;
  else
    err = streams_committed_offset_check(strName);

  EXPECT_EQ(err, out);
  ASSERT_EQ(0, stream_delete(strName, 1));
}

void regex_test (char *str1, char *str2, int type, bool isBlackList) {
  rd_kafka_resp_err_t err_expected = RD_KAFKA_RESP_ERR_NO_ERROR;
  rd_kafka_resp_err_t err = ConsumerTest::runRegexTest (str1, str2, type, isBlackList);
  switch (type) {
    case 0: break;

    case 1: err_expected =  RD_KAFKA_RESP_ERR__INVALID_ARG;
            break;

    case 2: err_expected =  RD_KAFKA_RESP_ERR__INVALID_ARG;
            break;

    case 3: break;

    default:
            break;
  }
  EXPECT_EQ (err, err_expected);
}

void read_i32 (unsigned char **ptr, int32_t *out) {
  memcpy (out, *ptr, sizeof(int32_t));
  *out = be32toh(*out);
  *ptr = *ptr + sizeof(int32_t);
}
void read_i16 (unsigned char **ptr, int16_t *out) {
  memcpy (out, *ptr, sizeof(int32_t));
  *out = be16toh(*out);
  *ptr = *ptr + sizeof(int16_t);
}

void verify_list_group(const char *groupid, struct rd_kafka_group_list * glist) {
  char *group1 = "MultiConGr";
  char *group2 = "singleConGr";
  char *clientid1 = "Consumer1";
  char *clientid2 = "Consumer2";
  char *clientid3 = "Consumer3";

  if(glist) {
    if (groupid)
      ASSERT_EQ(1, glist->group_cnt);
    for (int i =0; i < glist->group_cnt; i++) {
      cout << "\n--------------------------------" ;
      cout << "\nGroup: " << i;
      struct rd_kafka_group_info gi = glist->groups[i];
      cout << "\tName: " << gi.group;
      cout << "\tProtocol: " << gi.protocol;
      cout << "\tError: " << gi.err;
      cout << "\n--------------------------------" ;
      cout << "\n# of members: " << gi.member_cnt;
      if (strcmp (gi.group, group1)== 0)
        ASSERT_EQ(2, gi.member_cnt);
      else if (strcmp (gi.group, group2)== 0)
        ASSERT_EQ(1, gi.member_cnt);

      for (int j = 0; j < gi.member_cnt; j++ ) {
        struct rd_kafka_group_member_info mi = gi.members[j];
        cout << "\n\nMember: " << j;
        cout << "\nClient.id:" << mi.client_id;

        unsigned char *m_ass =(unsigned char *) mi.member_assignment;
        int16_t version = -1;
        int32_t tcount = 0;
        if (!m_ass)
          continue;
        read_i16( &m_ass, &version);
        cout << "\nVersion:" << version ;
        read_i32(&m_ass, &tcount);
        cout << "\n# of topics:" << tcount;

        if ((strcmp (gi.group, group1)== 0) &&
              (strcmp (mi.client_id, clientid1)==0))
          ASSERT_EQ(2, tcount);
        else if ((strcmp (gi.group, group1)== 0) &&
              (strcmp (mi.client_id, clientid2)==0))
          ASSERT_EQ(1, tcount);
        else if ((strcmp (gi.group, group2)== 0) &&
              (strcmp (mi.client_id, clientid3)==0))
          ASSERT_EQ(1, tcount);

        for (int32_t t = 0; t < tcount; t++) {
          int16_t str_len = 0;

          read_i16( &m_ass, &str_len);
          char str[str_len +1];
          memcpy (str, m_ass, str_len);
          str[str_len] = '\0';
          m_ass += str_len;
          cout << "\n" << t << ":Topic: " << str;
          int32_t partArrCnt = 0;
          read_i32 (&m_ass, &partArrCnt);
          cout << "\nPids:";
          for (int p = 0 ; p < partArrCnt ; p++) {
            int32_t part;
            read_i32 (&m_ass , &part);
            cout << "" << part << ",";
          }
        }
      } //end member_cnt for
    } // end group_cnt for
  } // end of if

  rd_kafka_group_list_destroy(glist);
}

void consumer_gr_list_test_case (char *strName, const char *groupid){
  ASSERT_EQ(0, stream_create(strName, 1, 4));
  sleep (1);
  struct rd_kafka_group_list * glist;
  EXPECT_EQ(RD_KAFKA_RESP_ERR_NO_ERROR,
          ConsumerTest::runConsumerListTest (strName, groupid,
                            (const struct rd_kafka_group_list **) &glist));

  verify_list_group(groupid, glist);
  ASSERT_EQ(0, stream_delete(strName, 1));
}

/*-----------------------------------------------*/
/*Consumer Create Tests*/
/*-----------------------------------------------*/
TEST(ConsumerTest, consumerCreateDefaultTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runConsumerCreateTest (true, true));
}
/*NULL conf is valid input*/
TEST(ConsumerTest, consumerCreateNullConfTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runConsumerCreateTest (false, true));
}
/*NULL type is valid input, defaults to RD_KAFKA_PRODUCER*/
TEST(ConsumerTest, consumerCreateNullTypeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runConsumerCreateTest (true, false));
}
TEST(ConsumerTest, consumerCreateInvalidArgTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runConsumerCreateTest (false, false));
}

/*-----------------------------------------------*/
/*Consumer Subscribe/Subscription Tests*/
/*-----------------------------------------------*/

TEST_F(SubscribeTest, maprConsumerDefaultSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 1, 1, 1, true,
                                                     0, 0, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerDefaultSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                                     0, 1, "ConsumerTest",
                                                     false, NULL ));
}
TEST_F(SubscribeTest, maprConsumerInvalidTypeSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 0, "ConsumerTest",
                                      false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerInvalidTypeSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 1, "ConsumerTest",
                                      false, NULL ));
}
TEST_F(SubscribeTest, maprConsumerInvalidGroupSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 0, NULL,
                                      false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerInvalidGroupSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 1, NULL,
                                      false, NULL ));
}
TEST_F(SubscribeTest, maprConsumerMaprTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 2, 2, 4, true,
                                                     1, 0, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, maprConsumerKafkaTopicSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     1, 1, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, maprConsumerMixedTopicSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     1, 2, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerKafkaTopicSubscribeTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 1, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerMaprTopicSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 0, "ConsumerTest",
                                                     false, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerMixedTopicSubscribeTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                                     2, 2, "ConsumerTest",
                                                     false, NULL));
}

/*-----------------------------------------------*/
/*Consumer Assign/Assignment Tests*/
/*-----------------------------------------------*/

TEST_F(SubscribeTest, maprConsumerDefaultAssignTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 1, 1, 1, true,
                                                     0, 0, "ConsumerTest",
                                                     true, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerDefaultAssignTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                                     0, 1, "ConsumerTest",
                                                     true, NULL));
}
TEST_F(SubscribeTest, maprConsumerInvalidTypeAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 0, "ConsumerTest",
                                      true, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerInvalidTypeAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, false,
                                      0, 1, "ConsumerTest",
                                      true, NULL));
}
TEST_F(SubscribeTest, maprConsumerInvalidGroupAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 0, NULL,
                                      true, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerInvalidGroupAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__UNKNOWN_GROUP,
      ConsumerTest::runSubscribeTest (strName,1, 1, 1, true,
                                      0, 1, NULL,
                                      true, NULL));
}
TEST_F(SubscribeTest, maprConsumerMaprTopicAssignTest) {
  EXPECT_EQ(SUCCESS, ConsumerTest::runSubscribeTest (strName, 2, 2, 4, true,
                                                     1, 0, "ConsumerTest", true, NULL));
}
TEST_F(SubscribeTest, DISABLED_maprConsumerKafkaTopicAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                      2, 1, "ConsumerTest", true, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerMaprTopicAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                      2, 0, "ConsumerTest", true, NULL));
}
TEST_F(SubscribeTest, kafkaConsumerMixedTopicAssignTest) {
  EXPECT_EQ(RD_KAFKA_RESP_ERR__INVALID_ARG,
      ConsumerTest::runSubscribeTest (strName,2, 2, 4, true,
                                      2, 2, "ConsumerTest", true, NULL));
}

/*-----------------------------------------------*/
/*Consumer Poll Tests*/
/*-----------------------------------------------*/
TEST(ConsumerTest, consumerPollSingleMsgTest) {
  consumer_poll_test_case (STREAM_POLL,1, 1, 1, 1, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                           "consumerPollSingleMsgTestGr", true, true, true );
}
TEST(ConsumerTest, consumerPollMultiStreamTest) {
  consumer_poll_test_case (STREAM_POLL, 4, 2, 2, 10000, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                           "consumerPollMultiStreamTestGr", true, true, true );
}
TEST(ConsumerTest, consumerPollMediumSizeMsgTest) {
    consumer_poll_test_case (STREAM_POLL, 2, 4, 2, 100, 9*1024*1024/10,
                             RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                             "consumerPollMediumMsgTestGr", true, true, true );
}
/*  librdkafka default msg size : 1000000
 *  TODO: Fix after config api is done
 */
TEST(ConsumerTest, DISABLED_consumerPollLargeSizeMsgTest) {
    consumer_poll_test_case (STREAM_POLL, 2, 4, 2, 100, 1*1024*1024,
                             RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                             "consumerPollMediumMsgTestGr", true, true, false );
}
TEST(ConsumerTest, consumerPollVerifyOrderTest) {
  consumer_poll_test_case (STREAM_POLL, 4, 2, 2, 10000, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                           "consumerPollVerifyOrderTestGr", true, true, false );
}
TEST(ConsumerTest, consumerPollVerifyOrderCommitTest) {
  consumer_poll_test_case (STREAM_POLL, 1, 1, 1, 10000, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                           "consumerPollVerifyOrderTestGr", true, false, false );
}
TEST(ConsumerTest, consumerPollVerifyOrderMsgFreeTest) {
  consumer_poll_test_case (STREAM_POLL, 4, 2, 2, 10000, 200,
                           RD_KAFKA_MSG_F_FREE, true, 0, false, 30,
                           "consumerPollVerifyOrderMsgFreeTestGr", true, true, true );
}
TEST(ConsumerTest, consumerSubscribeCommitTest) {
  consumer_poll_test_case (STREAM_POLL,1, 1, 1, 1000, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, true, 30,
                           "consumerSubscribeGr", true, false, true );
}
TEST(ConsumerTest, consumerAssignCommitTest) {
  consumer_poll_test_case (STREAM_POLL,1, 1, 1, 10, 200,
                           RD_KAFKA_MSG_F_COPY, true, 0, false, 30,
                           "consumerAssignGr", false, false, true );
}

/*-----------------------------------------------*/
/*Consumer Offset Commit Test*/
/*-----------------------------------------------*/
TEST(ConsumerTest, commitValidTopicTest) {
 consumer_offset_commit_test_case (STREAM_COMMIT, "commitValidTopicTestGr",
                                   false, false, false);
}
TEST(ConsumerTest, commitInvalidTopicTest) {
  consumer_offset_commit_test_case (STREAM_COMMIT, "commitInvalidTopicTestGr",
                                    false, true, false);
}
TEST(ConsumerTest, commitNullOffsetTest) {
  consumer_offset_commit_test_case (STREAM_COMMIT, "commitNullOffsetTestGr",
                                    false, false, true);
}
TEST(ConsumerTest, commitNullConsumerTest) {
  consumer_offset_commit_test_case (STREAM_COMMIT, "commitNullConsumerTestGr",
                                    true, false, false);
}

/*-----------------------------------------------*/
/*Consumer unsubscribe Test*/
/*-----------------------------------------------*/

TEST(ConsumerTest, consumerUnsubscribeTest) {
  ASSERT_EQ(0, stream_create (STREAM_UNSUBSCRIBE, 4, 2));
  EXPECT_EQ (8, ConsumerTest::runUnsubscribeTest (STREAM_UNSUBSCRIBE, 4, 2, 2,
                                                  1, 200, true, 0, true, 30,
                                                  "ConsumerTest"));
}
/*-----------------------------------------------*/
/*Consumer assign revoke cb test*/
/*-----------------------------------------------*/
TEST(ConsumerTest, DISABLED_consumerAssignRevokeCb) {
  ASSERT_EQ (0, stream_create(STREAM_UNSUBSCRIBE, 1, 12));
  ConsumerTest::runAssignRevokeCbTest (STREAM_UNSUBSCRIBE, 12);
}
/*-----------------------------------------------*/
/*Consumer close test*/
/*-----------------------------------------------*/

TEST(ConsumerTest, consumerCloseDefaultTest) {
  ASSERT_EQ (0, stream_create(STREAM_CLOSE, 1, 1));
  EXPECT_EQ (5 ,ConsumerTest::runConsumerCloseTest (STREAM_CLOSE,
                                            "consumerCloseGr", false));
}

TEST(ConsumerTest, consumerCloseNullTest) {
  ASSERT_EQ (0, stream_create(STREAM_CLOSE, 1, 1));
  EXPECT_EQ (RD_KAFKA_RESP_ERR__INVALID_ARG,
            ConsumerTest::runConsumerCloseTest (STREAM_CLOSE,
                                              "counsumerCloseGr",  true));
}

/*-----------------------------------------------*/
/*Consumer-Producer combination test*/
/*-----------------------------------------------*/
TEST (ConsumerTest, backToBackTest) {
  ASSERT_EQ (0, stream_create(STREAM_COMBINATION, 1, 1));
  EXPECT_EQ (0, ConsumerTest::runConsumerBack2BackTest(STREAM_COMBINATION));
}

/*-----------------------------------------------*/
/*Consumer seek-position combination test*/
/*-----------------------------------------------*/
TEST (ConsumerTest, seekPositionTest) {
  ASSERT_EQ (0, stream_create(STREAM_COMBINATION, 1, 1));
  ConsumerTest::runConsumerSeekPositionTest(STREAM_COMBINATION, "seekPositionGr",
                                            false);
}

/*-----------------------------------------------*/
/*Consumer regex subscription test*/
/*-----------------------------------------------*/
TEST_F(RegexTest, consumerRegexOnlySubscribeWithoutBlacklistTest) {
  regex_test (strName1, strName2, 0, false);
}
TEST_F(RegexTest, consumerRegexOnlySubscribeWithBlacklistTest) {
  regex_test (strName1, strName2, 0, true);
}
TEST_F(RegexTest, consumerSubscribeRegexSameStreamSubscribeTest) {
  regex_test (strName1, strName2, 1, false);
}
TEST_F(RegexTest, consumerRegexDiffStreamSubscribeTest) {
  regex_test (strName1, strName2, 2, false);
}
TEST_F(RegexTest, consumerStreamOnlySubscribeTest) {
  regex_test (strName1, strName2, 3, true);
}

/*-----------------------------------------------*/
/*Consumer gr list test*/
/*-----------------------------------------------*/
TEST (ConsumerTest, getAllConsumerGroupListTest) {
  consumer_gr_list_test_case (STREAM_CONSUMER_GR_LIST, NULL);
}

TEST (ConsumerTest, getConsumerGroupInfoTest) {
  consumer_gr_list_test_case (STREAM_CONSUMER_GR_LIST, "singleConGr");
}
/*-----------------------------------------------*/
/*Consumer unassign test*/
/*-----------------------------------------------*/

TEST (ConsumerTest, nullAssignTest) {
  ASSERT_EQ (0, stream_create(STREAM_UNASSIGN, 1, 10));
  ConsumerTest::runUnassignTest (STREAM_UNASSIGN, 2/*num of topics*/,
                                10/*num of patitions*/);
  ASSERT_EQ (0, stream_delete(STREAM_UNASSIGN, 1));
}

int main (int argc, char **argv) {
  testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS();
}
