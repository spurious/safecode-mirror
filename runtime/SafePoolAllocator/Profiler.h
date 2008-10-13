/// A simple profiler using performance counter

#ifndef _SC_PROFILER_H_
#define _SC_PROFILER_H_

#include <stdint.h>
#include "Config.h"
#include "rdtsc.h"

// #define PROFILING(X) X
#define PROFILING(X)

NAMESPACE_SC_BEGIN

typedef enum {
  PROFILER_MAIN_THR_BLOCK,
  PROFILER_CHECK,
  PROFILER_QUEUE_SIZE,
  PROFILER_MSG_TYPE_COUNT
} profiler_msg_type;

// Print a log to profiler
void profiler_log(int type, unsigned long long start_time, unsigned long long end_time, unsigned int tag);

NAMESPACE_SC_END

#endif
