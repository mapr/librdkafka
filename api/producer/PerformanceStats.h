#ifndef SRC_PERFORMANCE_STATS_H_
#define SRC_PERFORMANCE_STATS_H_
#include <iostream>
#include <stdint.h>
#include "../admin/Utils.h"

class PerformanceStats {
  private:
    uint64_t startTime_;
    uint64_t endTime_;
    int64_t minLatency_;
    int64_t maxLatency_;
    uint64_t totalLatency_;
    uint64_t totalBytes_;
    uint64_t msgCount_;
    uint64_t totalMsgsExpected_;
  public:
    bool run;
    PerformanceStats (int totalmsgs) {
                     startTime_ = 0;
                     endTime_ = 0;
                     minLatency_ = 999999;
                     maxLatency_ = 0;
                     totalLatency_ = 0;
                     totalBytes_ = 0;
                     msgCount_ = 0;
                     totalMsgsExpected_ = totalmsgs;
                     startTime_ = CurrentTimeMillis();
                     run = true;
    }
    bool checkAndVerify (bool print, uint64_t *numCb);
    void report (int64_t latency, int64_t bytes);
    void printReport (int numTopics, int numpartitions);
};
#endif
