/// This allocator is used for parallel checking, which puts
/// the execution of poolreigster / poolunregister into the checking 
/// thread

#include "ParPoolAllocator.h"

extern "C" {
  void __sc_par_pool_init_runtime(unsigned Dangling, unsigned RewriteOOB) {
    ParPoolAllocator::pool_init_runtime();
  }

  void __sc_par_poolinit(PoolTy *Pool, unsigned NodeSize) {
    ParPoolAllocator::poolinit(Pool, NodeSize);
  }

  void * __sc_par_poolalloc(PoolTy *Pool, unsigned NumBytes) {
    return ParPoolAllocator::poolalloc(Pool, NumBytes);
  }

  void __sc_par_poolfree(PoolTy *Pool, void *Node) {
    ParPoolAllocator::poolfree(Pool, Node);
  }

  void * __sc_par_poolrealloc(PoolTy *Pool, void *Node, unsigned NumBytes) {
    return PoolAllocatorFacade<ParPoolAllocator>::realloc(Pool, Node, NumBytes);
  }

  void * __sc_par_poolcalloc(PoolTy *Pool, unsigned Number, unsigned NumBytes) {
    return PoolAllocatorFacade<ParPoolAllocator>::calloc(Pool, Number, NumBytes);
  }

  void * __sc_par_poolstrdup(PoolTy *Pool, char *Node) {
    return PoolAllocatorFacade<ParPoolAllocator>::strdup(Pool, Node);
  }

}

