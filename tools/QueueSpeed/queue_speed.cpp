#include "../../runtime/SafePoolAllocator/ParPoolAllocator.h"

PoolTy Pool;

extern "C" void __sc_par_init_runtime(void);
extern "C" void __sc_par_poolcheck(PoolTy *, void *);
extern "C" void __sc_par_boundscheck(PoolTy*, void*, void*);
extern "C" void __sc_par_wait_for_completion(void);

int main() {

  __sc_par_init_runtime();
  __sc_par_poolinit(&Pool, 128);
  char* obj1 = (char*)__sc_par_poolalloc(&Pool, 128);
  char* obj2 = (char*)__sc_par_poolalloc(&Pool, 128);
  for (unsigned x = 0; x < 100000000; ++x) {
    __sc_par_poolcheck(&Pool, obj1 + (x % 128));
    __sc_par_boundscheck(&Pool, obj1, obj1 + (x % 128));
    //    __sc_par_poolcheck(&Pool, obj2 + (x % 128));
    //    __sc_par_boundscheck(&Pool, obj2, obj2 + (x % 128));
  }
  __sc_par_wait_for_completion();
  return 0;
}

extern "C" void poolcheckfail(void) {
  abort();
}
