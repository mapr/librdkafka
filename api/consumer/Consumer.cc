#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "../../src/rdkafka.h"
#include <sys/time.h>
#include "Consumer.h"
#include <unordered_map>
#include "ConsumerPerfStats.cc"
#include "../producer/Producer.h"

using namespace std;

char* Consumer::streamName;
int Consumer::numStreams;
int Consumer::numTopics;
int Consumer::numPartitions;
int Consumer::numMsgsPerPartition;
bool Consumer::printStats;
rd_kafka_t *consumer;
const char* Consumer::groupId;
bool Consumer::autoCommitEnabled;
bool topicSubscription = true;
bool Consumer::verifyKeys = false;
bool isTracingEnabled = true;
int pollsWithMissingMsgs = 0;
bool Consumer::verbose = false;
int allowedMissingPollCount = 3;
static std::unordered_map<std::string, int, keyHasher> offsetCommitMap;

static volatile int subscriptions_tp = 0;
static volatile int revoked_tp = 0;

void consumer_rebalance_cb(rd_kafka_t *rk,
                           rd_kafka_resp_err_t err,
                           rd_kafka_topic_partition_list_t *partitions,
                           void *opaque) {
  switch (err) {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
          break;

    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
          break;

    default:
      break;
  }
}

int verifyAndAddStats(rd_kafka_message_t *rkmessage, ConsumerPerfStats *stats,
                      int numTopics, int numParts, bool autoCommit, bool verify,
                      bool checkOrder, int *prevIdArr) {
  if(!rkmessage)
    return -1;

  uint64_t numBytes = rkmessage->len + rkmessage->key_len;
  int msg_partition = rkmessage->partition;
  const char *msg_topic_name =  rd_kafka_topic_name(rkmessage->rkt);
  if (verify && (rkmessage->key_len!=0)) {
    char *tokens[7];
    int tok = 0;
    bool invalidKey = false;
    tokens[tok]	= strtok((char *)rkmessage->key , ":");

    while(tok < 7) {
      tok++;
      if(tok == 7) {
        break;
      }
      tokens[tok] = strtok (NULL , ":");
      if(!tokens[tok] && tok <= 7 ) {
        invalidKey = true;
        break;
      }
    }
    if(invalidKey) {
        std::cerr << "\nERROR:: invalid key format " << (char *)rkmessage->key;
        std::cerr << " Expected: \
          'prefix:streamName:topicName:StreamId:TopicId:partitionId:msgId'" ;
      return -1;
    }
    int streamId = atoi(tokens[3]);
    int topicId = atoi(tokens[4]);
    int partition = atoi(tokens[5]);
    int msgId = atoi(tokens[6]);
    if (checkOrder) {
      int index = streamId*numTopics*numParts + topicId*numParts + partition;
      if(msgId < prevIdArr[index]) {
        std::cerr << "\nERROR::Messages Out of order,message id in Key " << msgId;
        std::cerr << " mismatched. Expected > " << prevIdArr[index] ;
        return -1;
      }
      else
        prevIdArr[index] = msgId;
    }

    char tempName[100];
    memset(tempName, '\0', 100);
    sprintf(tempName, "%s:%s",  tokens[1], tokens[2]);
    if(strcmp(tempName, msg_topic_name) != 0) {
      std::cerr << "\nERROR:: Topic in Key " << tempName;
      std::cerr << " mismatched. Expected " << msg_topic_name ;
      return -1;
    }
    if (partition != msg_partition) {
      std::cerr << "\nERROR:: Partition in Key " << partition;
      std::cerr << " mismatched. Expected " << msg_partition ;
      return -1;
    }
  }//end verifyKeys

  if (!autoCommit) {
    char msgTopicStr[strlen(msg_topic_name)+1];
    strncpy(msgTopicStr, msg_topic_name, strlen(msg_topic_name));
    msgTopicStr[strlen(msg_topic_name)+1] = '\0';

    char newKey[200];
    memset(newKey, '\0', 200);
    sprintf(newKey, "%s:%d", msgTopicStr, msg_partition );
    offsetCommitMap[newKey] = rkmessage->offset + 1;
  }
  stats->report(numBytes, 1);

  return 0;
}

void get_commit_offset_list(std::unordered_map<std::string, int, keyHasher> offsetMap
                          , rd_kafka_topic_partition_list_t **outList){

	for(unordered_map<string, int, keyHasher>::iterator it = offsetMap.begin();
                                                  it != offsetMap.end(); ++it) {
		std::string k = it->first;
		int len = strlen(k.c_str());
		int pos = k.rfind(':');
		std::string rdtopic = k.substr(0, pos);
		int id = atoi((k.substr(pos+1 , len)).c_str());
		rd_kafka_topic_partition_t *rktpar =
              rd_kafka_topic_partition_list_add(*outList, rdtopic.c_str(), id);
		rktpar->offset = it->second;
	}
}

uint64_t Consumer::run() {
	if ((streamName == NULL) || (strlen(streamName) == 0)) {
	  return INVALID_ARGUMENT;
	}
	if ((groupId == NULL) || (strlen(groupId) == 0)) {
	  return INVALID_ARGUMENT;
	}

	char errstr[512];
	rd_kafka_conf_t *conf = rd_kafka_conf_new();
  if (autoCommitEnabled)
	  rd_kafka_conf_set(conf, "enable.auto.commit", "true", errstr, sizeof(errstr));
  else
	  rd_kafka_conf_set(conf, "enable.auto.commit", "false", errstr, sizeof(errstr));

  rd_kafka_conf_set(conf, "group.id", groupId, errstr, sizeof(errstr));

	rd_kafka_conf_set_rebalance_cb(conf, consumer_rebalance_cb);

	struct RebalanceCbCtx	rb_ctx;
	rd_kafka_conf_set_opaque(conf, &rb_ctx);
	rd_kafka_conf_set(conf, "auto.offset.reset", "earliest",
                                    errstr, sizeof(errstr));

	if (!(consumer = rd_kafka_new(RD_KAFKA_CONSUMER, conf,
                                    errstr, sizeof(errstr)))) {
	return CONSUMER_CREATE_FAILED;
	}

	int nTotalTp = numStreams * numTopics * numPartitions;
	rd_kafka_topic_partition_list_t *tp_list =
                              rd_kafka_topic_partition_list_new(nTotalTp);

	for (int s = 0; s <  numStreams; ++s) {
		for (int t = 0; t < numTopics; ++t) {
		  char currentName[100];
			memset ( currentName, '\0', 100);
			int tempIndex = s* numTopics +t;
			sprintf(currentName, "%s%d:topic%d",  streamName, s, t);
			if (topicSubscription) {
			   rd_kafka_topic_partition_list_add(tp_list, currentName,
                                          RD_KAFKA_PARTITION_UA);
			} else{
			    for(int p = 0; p < numPartitions ; ++p )
				    rd_kafka_topic_partition_list_add(tp_list, currentName , p);
      }
		}
	}
  rd_kafka_topic_partition_list_t *outList = rd_kafka_topic_partition_list_new(0);
  if (topicSubscription) {
    rb_ctx.subscriptionCnt = tp_list->cnt;
    rd_kafka_subscribe(consumer, tp_list);
    sleep (2);
    if(verifyKeys)
      rd_kafka_subscription (consumer, &outList);
  } else {
    rd_kafka_assign(consumer, tp_list);
    if(verifyKeys)
      rd_kafka_assignment (consumer, &outList);
  }
  for (int i=0; i < outList->cnt; i++) {
    cout << "\n topic: " << outList->elems[i].topic;
    cout << "\t partition: " << outList->elems[i].partition;
    cout << "\t offset: " << outList->elems[i].offset;
  }

  int prevMsgId[nTotalTp];
  std::fill_n(prevMsgId, nTotalTp, -1);
  ConsumerPerfStats *stats = new ConsumerPerfStats();
  int msgCount = 0;
	while (true) {
		rd_kafka_message_t *rkmessage;
		rkmessage = rd_kafka_consumer_poll(consumer, 1000);
		if (rkmessage) {
			msgCount++;
			pollsWithMissingMsgs = 0;
			verifyAndAddStats(rkmessage, stats, numTopics, numPartitions,
                        autoCommitEnabled, verifyKeys,
                        true/*checkOrder*/, prevMsgId);
		} else {
			++pollsWithMissingMsgs;
		}
		if (pollsWithMissingMsgs >= allowedMissingPollCount ) {
			break;
		}
    if (msgCount >= ( nTotalTp * numMsgsPerPartition))
      break;
	}

	if (printStats)
		stats->printReport();

  int commitResult = -1;
  if (!autoCommitEnabled) {
    rd_kafka_topic_partition_list_t *c_offsets = rd_kafka_topic_partition_list_new(1);
    get_commit_offset_list(offsetCommitMap, &c_offsets);
    commitResult = rd_kafka_commit(consumer, c_offsets, 0);
    rd_kafka_topic_partition_list_destroy(c_offsets);
    //TODO: Verify committed offset
  }
  sleep (5);
  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy(consumer);
  if (!autoCommitEnabled && commitResult != 0)
    return commitResult;
  else
    return stats->getMsgCount();
}

uint64_t Consumer::runTest (char *path, int nStreams, int nTopics, int nParts,
                      int nMsgs, const char* groupid, bool topicSub,
                      bool autoCommit, bool verify, bool print) {
  streamName = path;
	numStreams = nStreams;
	numTopics = nTopics;
	numPartitions = nParts;
	numMsgsPerPartition = nMsgs;
	printStats = print;
	groupId = groupid;
	autoCommitEnabled = autoCommit;
	topicSubscription = topicSub;
	verifyKeys = verify;
  return run();
}
