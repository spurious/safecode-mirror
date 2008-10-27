#include "../../runtime/SafePoolAllocator/ParPoolAllocator.h"

PoolTy Pool;

extern "C" void __sc_par_init_runtime(void);
extern "C" void __sc_par_poolcheck(PoolTy *Pool, void *Node);
extern "C" void __sc_par_boundscheck(PoolTy * Pool, void * Source, void * Dest);
extern "C" void __sc_par_wait_for_completion(void);

int main() {

  __sc_par_init_runtime();
  __sc_par_poolinit(&Pool, 128);
  char* obj = (char*)__sc_par_poolalloc(&Pool, 128);
  for (unsigned x = 0; x < 100000000; ++x) {
    __sc_par_poolcheck(&Pool, obj + (x % 128));
    __sc_par_boundscheck(&Pool, obj, obj + (x % 128));
  }
  __sc_par_wait_for_completion();
  return 0;
}

extern "C" void poolcheckfail(void) {
  abort();
}
