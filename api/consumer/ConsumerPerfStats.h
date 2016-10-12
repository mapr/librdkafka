#ifndef SRC_CONSUMER_PERF_STATS_H_
#define SRC_CONSUMER_PERF_STATS_H_
#include <iostream>
#include <stdint.h>
#include "../admin/Utils.h"

class ConsumerPerfStats {
	private:
		uint64_t startTime_;
		uint64_t endTime_;
		uint64_t totalBytes_;
		uint64_t totalMsgs_;

	public:
		ConsumerPerfStats() {
			startTime_ = CurrentTimeMillis();
			endTime_ = -1;
			totalBytes_ = 0;
			totalMsgs_ = 0;
		}

		void addBytes (uint64_t bytes);
		void report(uint64_t bytes, uint64_t numMsg);
		void printReport();
		uint64_t getMsgCount();
};


#endif
