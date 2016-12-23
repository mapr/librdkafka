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
#include "../common/ErrorCodeTest.h"
#include "../common/ErrorCodeTest.cc"

char *topic = "err_test_topic"; // Created topic
char *strName = "/gtest-ErrorCodeTest";
void run_produce_error_code_test (char *strName, char *topic, int pid, int msgFlag,
                                  char *value, int vLen, char *key, int kLen) {
  ErrStruct *err_kafka = (ErrStruct *)malloc (sizeof(ErrStruct));
  ErrStruct *err_streams = (ErrStruct *)malloc (sizeof(ErrStruct));

    err_kafka =  ErrorCodeTest::runRdKafkaProduceApiErrorCodeTest(topic, pid, msgFlag,
                                          (void *) value, vLen,
                                          (const void *)key, kLen,
                                          NULL, (void *)err_kafka);

  char fullName[100];
  if (topic != NULL) {
    snprintf (fullName, sizeof (fullName),
        "%s0:%s", strName, topic);
    err_streams =  ErrorCodeTest::runRdKafkaProduceApiErrorCodeTest(
                                          fullName, pid, msgFlag,
                                          (void *) value, vLen,
                                          (const void *)key, kLen,
                                          NULL, (void *)err_streams);

  } else {
      err_streams =  ErrorCodeTest::runRdKafkaProduceApiErrorCodeTest(NULL, pid, msgFlag,
                                          (void *) value, strlen (value),
                                          (const void *)key, kLen,
                                          NULL, (void *)err_streams);

  }
  EXPECT_EQ(err_kafka->apiErr, err_streams->apiErr);
  EXPECT_EQ(err_kafka->apiCbErr, err_streams->apiCbErr);
  EXPECT_EQ(err_kafka->errCode, err_streams->errCode);

  free (err_kafka);
  free (err_streams);
}

void run_subscribe_error_code_test (char *strName, char *topic,
                                    bool isAssign,
                                    bool addNonExistentTopic, bool commit) {

  ErrStruct *err_kafka = (ErrStruct *)malloc (sizeof(ErrStruct));
  ErrStruct *err_streams = (ErrStruct *)malloc (sizeof(ErrStruct));

  err_kafka =  ErrorCodeTest::runSubscribeErrorCodeTest(NULL, topic,
                                            true/*isKafka*/, isAssign,
                                            addNonExistentTopic,
                                            commit, (void *)err_kafka);

  err_streams =  ErrorCodeTest::runSubscribeErrorCodeTest(strName, topic,
                                            false/*isKafka*/, isAssign,
                                            addNonExistentTopic,
                                            commit, (void *)err_streams);
  EXPECT_EQ(err_kafka->apiErr, err_streams->apiErr);
  EXPECT_EQ(err_kafka->apiCbErr, err_streams->apiCbErr);

  free (err_kafka);
  free (err_streams);
}


/*-----------------------------------------------*/
/* Producer err tests*/
/*-----------------------------------------------*/
TEST(ErrorCodeTest, msgProduceDefaultTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, topic, 0, 0, value, strlen(value), NULL, 0);
}

TEST(ErrorCodeTest, msgProduceMsgFlagCopyTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, topic, 0, RD_KAFKA_MSG_F_COPY,
                                    value, strlen(value), NULL, 0);
}

TEST(ErrorCodeTest, DISABLED_msgProduceMsgFlagFreeTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, topic, 0, RD_KAFKA_MSG_F_FREE,
                               value, strlen(value), NULL, 0);
}

TEST(ErrorCodeTest, msgProduceNullTopicTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, NULL, 0, 0,
                               value, strlen(value), NULL, 0);
}

TEST(ErrorCodeTest, msgProduceInvalidKeyValTest) {
  run_produce_error_code_test (strName, topic, 0, 0, NULL, 0, NULL, 0);
}

TEST(ErrorCodeTest, msgProduceInvalidPartitionTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, topic, 0, -1,
                               value, strlen(value), NULL, 0);
}

TEST(ErrorCodeTest, msgProduceOutOfBoundPartitionTest) {
  char * value = (char *) malloc (100 * sizeof (char));
  memset (value, 'a', 100);
  run_produce_error_code_test (strName, topic, 0, 100,
                               value, strlen(value), NULL, 0);
}
TEST(ErrorCodeTest, msgProduceMaxMsgSizeTest) {
  char * max_value = (char *) malloc (10000000 * sizeof (char));
  memset (max_value, 'a', 10000000);
  run_produce_error_code_test (strName, topic, 0, 100,
                               max_value, strlen(max_value), NULL, 0);
}

/*-----------------------------------------------*/
/* Consumer err tests*/
/*-----------------------------------------------*/
TEST(ErrorCodeTest, existingTopicSubscribeTest) {
  run_subscribe_error_code_test (strName, topic, false, false, false);
}

TEST(ErrorCodeTest, DISABLED_nonExistingTopicSubscribeTest) {
  run_subscribe_error_code_test (strName, "RandomTopic", false, false, false);
}
TEST(ErrorCodeTest, mixofExistingAndNonExistingTopicSubscribeTest) {
  run_subscribe_error_code_test (strName, topic, false, true, false);
}

TEST(ErrorCodeTest, existingTopicAssignTest) {
  run_subscribe_error_code_test (strName, topic, true, false, false);
}

TEST(ErrorCodeTest, DISABLED_nonExistingTopicAssignTest) {
  run_subscribe_error_code_test (strName, "RandomTopic", true, false, false);
}
TEST(ErrorCodeTest, mixofExistingAndNonExistingTopicAssignTest) {
  run_subscribe_error_code_test (strName, topic, true, true, false);
}

TEST(ErrorCodeTest, existingTopicSubscribeCommitTest) {
  run_subscribe_error_code_test (strName, topic, false, false, true);
}

TEST(ErrorCodeTest, DISABLED_nonExistingTopicSubscribeCommitTest) {
  run_subscribe_error_code_test (strName, "RandomTopic", false, false, true);
}
TEST(ErrorCodeTest, mixofExistingAndNonExistingTopicSubscribeCommitTest) {
  run_subscribe_error_code_test (strName, topic, false, true, true);
}
/* 0.9.1 has kafka bug around assign and commit. It hangs during destroy call.
   * https://github.com/edenhill/librdkafka/issues/729
   * Disabling test untill that is pulled
   */
TEST(ErrorCodeTest, DISABLED_existingTopicAssignCommitTest) {
  run_subscribe_error_code_test (strName, topic, true, false, true);
}

TEST(ErrorCodeTest, DISABLED_nonExistingTopicAssignCommitTest) {
  run_subscribe_error_code_test (strName, "RandomTopic", true, false, true);
}
TEST(ErrorCodeTest, DISABLED_mixofExistingAndNonExistingTopicAssignCommitTest) {
  run_subscribe_error_code_test (strName, topic, true, true, true);
}
TEST (ErrorCodeTest, PartitionEOFTest) {
  int kafka_err = ErrorCodeTest::runPartitionEOFErrorCodeTest(strName, topic,
                                                              true, NULL);
  EXPECT_EQ (kafka_err, ErrorCodeTest::runPartitionEOFErrorCodeTest(strName,
                                                        topic, false, NULL));
}

int main (int argc, char **argv) {
  system("../admin/ErrorTestSetup.sh");
  testing::InitGoogleTest(&argc, argv);
  int out = RUN_ALL_TESTS();
  stream_delete("/gtest-ErrorCodeTest", 1);
  system ("kill -9 $(lsof -t -i:9092)");
  return out;
}
