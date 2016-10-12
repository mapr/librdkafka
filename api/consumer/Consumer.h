#ifndef SRC_CONSUMER_H_
#define SRC_CONSUMER_H_
#include "ConsumerPerfStats.h"
#define HASH_SEED 0x7fffffff
#define HASH_MASK 0xffffffffffffff00ULL

using namespace std;

class Consumer {
	private:
	static char *streamName;
	static int numStreams;
	static int numTopics;
	static int numPartitions;
	static int numMsgsPerPartition;
	static const int MSG_VALUE_LENGTH = 200;
	static const int MSG_KEY_LENGTH = 200;
	static bool printStats;
	static bool verbose;
	static const char *groupId;
	static bool autoCommitEnabled;
  static bool verifyKeys;
  public:
	rd_kafka_t *consumer;
	struct RebalanceCbCtx {
		int subscriptionCnt;
		int unSubscribedCnt;
		ConsumerPerfStats *stats;
	};

	Consumer();
	Consumer (char* s, int nstreams, int ntopics, int npart, int nmsg,
           const char* groupid, bool  autoCommit, bool verifyKeys);
	~Consumer();
	uint64_t run();
	uint64_t runTest (char* path, int nStreams, int nTopics, int nParts, int nMsgs,
               const char* groupid, bool topicSub, bool autoCommit,
               bool verifyKeys, bool print);
	void usage();
};

void consumer_msg_consume_cb(rd_kafka_message_t *rkmessage,
                             void *opaque);

void consumer_rebalance_cb(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                            rd_kafka_topic_partition_list_t *partitions,
                            void *opaque);

Consumer::Consumer () {
	streamName = "";
	numStreams = 2;
	numTopics = 2;
	numPartitions = 4;
	numMsgsPerPartition = 10000;
	printStats = false;
	groupId = "";
	autoCommitEnabled = true;
	verifyKeys = false;
}

Consumer::Consumer (char* s, int nstreams, int ntopics, int npart, int nmsg,
                    const char *groupid, bool autoCommit, bool verify) {
	streamName = s;
	numStreams = nstreams;
	numTopics = ntopics;
	numPartitions = npart;
	numMsgsPerPartition = nmsg;
	printStats = true;
	groupId = groupid;
	autoCommitEnabled = true;
	verifyKeys = verify;
}

Consumer::~Consumer() {
}


static uint64_t
MurmurHash64A (const void * key, int len)
{
	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;
	uint64_t h = HASH_SEED ^ (len * m);
	const uint64_t * data = (const uint64_t *)key;
	const uint64_t * end = data + (len/8);
	while(data != end) {
		uint64_t k = *data++;
		k *= m;
		k ^= k >> r;
		k *= m;
		h ^= k;
		h *= m;
	}

	const unsigned char * data2 = (const unsigned char*)data;
	switch(len & 7) {
		case 7: h ^= ((uint64_t)data2[6]) << 48;
		case 6: h ^= ((uint64_t)data2[5]) << 40;
		case 5: h ^= ((uint64_t)data2[4]) << 32;
		case 4: h ^= ((uint64_t)data2[3]) << 24;
		case 3: h ^= ((uint64_t)data2[2]) << 16;
		case 2: h ^= ((uint64_t)data2[1]) << 8;
		case 1: h ^= ((uint64_t)data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;
	return h;
}

size_t strHash(const char *key)
{
	uint32_t len = strlen(key);
	uint64_t hash = MurmurHash64A(key, len);
	// trim to 56 bits.
	hash = ((hash & 0xff) << 56) ^ (hash & HASH_MASK);
	return (hash ? hash : 1);
}

struct keyHasher {
	std::string keystr;

	size_t operator()(const std::string& t) const {
		size_t hash =  strHash(t.c_str());//calculate hash here.
		return hash;
	}

	bool operator==(const std::string &s) const
	{
		if (strcmp(keystr.c_str(), s.c_str()) == 0)
			return true;
		return false;
	}
};

#endif
