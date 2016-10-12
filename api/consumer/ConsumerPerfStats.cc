#include <iostream>
#include <stdint.h>
#include "ConsumerPerfStats.h"

using namespace std;

void ConsumerPerfStats::addBytes (uint64_t bytes) {
      atomic_add64(&totalBytes_, bytes);
}

void ConsumerPerfStats::report(uint64_t bytes, uint64_t numMsgs) {
      atomic_add64(&totalBytes_, bytes);
	    atomic_add64(&totalMsgs_, numMsgs);
}

void ConsumerPerfStats::printReport() {
	endTime_ = CurrentTimeMillis();
	int64_t  elapsedTime =  endTime_ -  startTime_;
	cout << "Total time (ms): " <<  elapsedTime << "\n";
	cout << "Total bytes received: " <<  totalBytes_ << "\n";
	cout << "Total messages received: " <<  totalMsgs_ << "\n";
	//uint64_t bytesInKb = totalBytes_ /1024;
	cout << "Average nMsgs/sec: " <<  totalMsgs_*1.0/ elapsedTime*1000.0 << "\n";
	cout << "Average nKBs/sec: " <<  totalBytes_*1.0/ elapsedTime*1000.0/1024.0 << "\n";
}

uint64_t ConsumerPerfStats::getMsgCount() {
  return totalMsgs_;
}
