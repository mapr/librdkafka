#include <iostream>
#include <stdint.h>
#include "PerformanceStats.h"
using namespace std;

bool PerformanceStats::checkAndVerify(bool print, uint64_t *numCb) {
  *numCb = msgCount_;
  if (totalMsgsExpected_ != msgCount_) {
    if(print)
      std::cout << "\n***** Verification failed! *****\n" ;
    return false;
  }
  return true;
}

void PerformanceStats::report( int64_t latency, int64_t bytes) {
  if (latency > 0) {
    if ( minLatency_ > latency)  minLatency_ = latency;
    if ( maxLatency_ < latency)  maxLatency_ = latency;
  }

  atomic_add64(&totalBytes_, bytes);
  atomic_add64(&msgCount_, 1);
  atomic_add64(&totalLatency_, latency);

  if ( msgCount_ ==  totalMsgsExpected_)
  {
    endTime_ = CurrentTimeMillis();
    run = false;
  }
}

void PerformanceStats::printReport(int numTopics, int numpartitions) {
  std::cout << "Expected nMsgs: " <<  totalMsgsExpected_ << "\n";
  std::cout << "Callback nMsgs: " <<  msgCount_ << "\n";

  if ( endTime_ == 0)
    endTime_ = CurrentTimeMillis();

  int64_t  elapsedTime =  endTime_ -  startTime_;
  std::cout << "Total time (ms): " <<  elapsedTime << "\n";
  std::cout << "Total bytes sent: " <<  totalBytes_ << "\n";
  std::cout << "Min/Max latency (ms): " <<  minLatency_ << "/" <<  maxLatency_ << "\n";
  std::cout << "Average latency (ms): " << ( totalLatency_*1.0/ msgCount_) << "\n";
  std::cout << "Average nMsgs/sec: " <<  msgCount_*1.0/ elapsedTime*1000.0 << "\n";
  std::cout << "Average nKBs/sec: " <<  totalBytes_*1.0/ elapsedTime*1000.0/1024.0 << "\n";
}
