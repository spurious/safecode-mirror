/// This pool allocator registers objects into splay tree
/// to perform memory access checking.

#include "PoolAllocator.h"
#include "adl_splay.h"
#include <cassert>

class BCPoolAllocator {
public:
  typedef PoolTy PoolT;
  static void * poolalloc(PoolTy *Pool, unsigned NumBytes) {
    void * ret = __barebone_poolalloc(Pool, NumBytes);
    poolregister(Pool, ret, NumBytes);
    return ret;
  }

  static void * pool_alloca(PoolTy * Pool, unsigned int NumBytes) {
    assert (0 && "Should be deprecated\n");
    void * ret = __barebone_pool_alloca(Pool, NumBytes);
    poolregister(Pool, ret, NumBytes); 
    return ret;
  }

  static void poolinit(PoolTy *Pool, unsigned NodeSize) {
    __barebone_poolinit(Pool, NodeSize);
  }

  static void pooldestroy(PoolTy *Pool) {
    __barebone_pooldestroy(Pool);
    adl_splay_clear(&Pool->Objects);
    assert (Pool->Objects == 0);
  }
  
  static void pool_init_runtime() {
    // Disable dangling pointer checkings
    ::pool_init_runtime(0);
  }

  static void poolfree(PoolTy *Pool, void *Node) {
    __barebone_poolfree(Pool, Node);
    poolunregister(Pool, Node);
  }
};


extern "C" {
  void __sc_bc_pool_init_runtime(unsigned Dangling) {
    BCPoolAllocator::pool_init_runtime();
  }

  void __sc_bc_poolinit(PoolTy *Pool, unsigned NodeSize) {
    BCPoolAllocator::poolinit(Pool, NodeSize);
  }

  void __sc_bc_pooldestroy(PoolTy *Pool) {
    BCPoolAllocator::pooldestroy(Pool);
  }

  void * __sc_bc_poolalloc(PoolTy *Pool, unsigned NumBytes) {
    return BCPoolAllocator::poolalloc(Pool, NumBytes);
  }
  
  void __sc_bc_poolfree(PoolTy *Pool, void *Node) {
    BCPoolAllocator::poolfree(Pool, Node);
  }

  void * __sc_bc_poolrealloc(PoolTy *Pool, void *Node, unsigned NumBytes) {
    return PoolAllocatorFacade<BCPoolAllocator>::realloc(Pool, Node, NumBytes);
  }

  void * __sc_bc_poolcalloc(PoolTy *Pool, unsigned Number, unsigned NumBytes) {
    return PoolAllocatorFacade<BCPoolAllocator>::calloc(Pool, Number, NumBytes);
  }

  void * __sc_bc_poolstrdup(PoolTy *Pool, char *Node) {
    return PoolAllocatorFacade<BCPoolAllocator>::strdup(Pool, Node);
  }

}

