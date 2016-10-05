#ifndef UTILS_SRC_H_
#define UTILS_SRC_H_
#include <stdio.h>
#include <sys/time.h>

#define ARRAY_SIZE(array) (sizeof((array))/sizeof((array[0])))
#ifdef __GNUC__
#define atomic_add32(ptr, val) __sync_fetch_and_add ((ptr), (val))
#define atomic_sub32(ptr, val) __sync_fetch_and_sub ((ptr), (val))
#define atomic_add64(ptr, val) __sync_fetch_and_add ((ptr), (val))
#define atomic_sub64(ptr, val) __sync_fetch_and_sub ((ptr), (val))
#define atomic_inc32(ptr) __sync_add_and_fetch ((ptr), (1))
#define atomic_dec32(ptr) __sync_sub_and_fetch ((ptr), (1))
#define atomic_inc64(ptr) __sync_add_and_fetch ((ptr), (1))
#define atomic_dec64(ptr) __sync_sub_and_fetch ((ptr), (1))
#endif

enum STREAM_ERROR_CODES {
  INVALID_ARGUMENT = -1,
  SUCCESS = 0,
  STREAM_CREATE_FAILED = 1,
  STREAM_DELETE_FAILED = 2,
  PRODUCER_CREATE_FAILED =3,
  PRODUCER_SEND_FAILED = 4,
  PRODUCER_POLL_FAILED = 5,
};


static uint64_t CurrentTimeMillis() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

static int stream_create(const char * streamName,
                         int numStreams, int numParts) {
  char command[1000];
  int res= 0;
  for(int s=0; s <numStreams; s++) {
    memset(command, 0, 1000);
    sprintf(command, "maprcli stream delete -path %s%d 2>/dev/null ",
                                                      streamName, s);
    system(command);
    sleep(1);
    memset(command, 0, 1000);
    sprintf(command, "maprcli stream create -path %s%d \
          -defaultpartitions %d 2>/dev/null ", streamName, s, numParts);
    res += system(command);
  }
  return res;
}

static int stream_delete(const char * streamName, int numStreams) {
  char command[1000];
  int res = 0;
  for(int s = 0; s<numStreams; s++) {
    memset(command, 0, 1000);
    sprintf(command, "maprcli stream delete -path %s%d 2>/dev/null ",
                                                      streamName, s);
    res += system(command);
  }
  return res;
}

static uint64_t stream_count_check(const char * streamName, int numStreams) {
  char command[1000];
  uint64_t res = 0;
  for(int s = 0; s < numStreams; s++) {
    try {
      memset(command, 0, 1000);
      sprintf(command, "mapr streamanalyzer -path %s%d 2>/dev/null | \
          awk '{print $NF}'| paste -s", streamName, s);
      char buf[128];
      FILE* pipe = popen(command, "r");
      if (!pipe) throw std::runtime_error("popen() failed!");
      while (!feof(pipe)) {
        if (fgets(buf, sizeof(buf), pipe) != NULL) {
          res += (uint64_t)atoi(buf);
        }
      }
      pclose(pipe);
    } catch (const std::exception& e) {
      std::cerr << "\nstream_count_check: Exception occured,";
      std::cerr << e.what() << "\n";
      return INVALID_ARGUMENT;
    }
  }
  return res;
}
#endif
