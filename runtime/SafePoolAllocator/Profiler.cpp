/// Implementation of simple profiler

#include "Profiler.h"
#include <pthread.h>
#include <stdio.h>

NAMESPACE_SC_BEGIN

static const char * LOG_FILENAME = "/localhome/mai4/profile.bin";

class Profiler {
public:
  Profiler() {
    // There's no pthread_spin_lock in Mac OS X
    // Probably we need to implement one.
//    pthread_spin_init(&m_lock, 0);
    m_log = fopen(LOG_FILENAME, "wb");
  }

  ~Profiler() {
//    pthread_spin_destroy(&m_lock);
    fclose(m_log);
  }

  void log(int type, unsigned long long start_time, unsigned long long end_time, unsigned int tag) {
//    pthread_spin_lock(&m_lock);
    fwrite(&type, sizeof(int), 1, m_log);
    fwrite(&start_time, sizeof(unsigned long long), 1, m_log);
    fwrite(&end_time, sizeof(unsigned long long), 1, m_log);
    fwrite(&tag, sizeof(unsigned int), 1, m_log);
//    pthread_spin_unlock(&m_lock);
  }
    
private:
  FILE * m_log;
//    pthread_spinlock_t m_lock;
  
};

PROFILING (static Profiler p;)

void profiler_log(int type, unsigned long long start_time, unsigned long long end_time, unsigned int tag)
{
  PROFILING (p.log(type, start_time, end_time, tag);)
}

NAMESPACE_SC_END
