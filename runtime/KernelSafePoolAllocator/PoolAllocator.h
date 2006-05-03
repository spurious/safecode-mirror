//===- PoolAllocator.h - Pool allocator runtime interface file --*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines the interface which is implemented by the LLVM pool
// allocator runtime library.
//
//===----------------------------------------------------------------------===//

#ifndef POOLALLOCATOR_RUNTIME_H
#define POOLALLOCATOR_RUNTIME_H
#include "Support/hash_set"
#include "splay.h"
#include <stdarg.h>

#define AddrArrSize 2
unsigned poolmemusage = 0;
unsigned PCheckPassed = 1;

typedef struct PoolTy {
  // Ptr1, Ptr2 - Implementation specified data pointers.
  void *Ptr1, *Ptr2;

  // NodeSize - Keep track of the object size tracked by this pool
  unsigned short NodeSize;

  // FreeablePool - Set to false if the memory from this pool cannot be freed
  // before destroy.
  //
  //  unsigned short FreeablePool;

  // Use the hash_set only if the number of Slabs exceeds AddrArrSize
  hash_set<void*> *Slabs;

  // The array containing the initial address of slabs (as long as there are
  // fewer than a certain number of them)
  unsigned SlabAddressArray[AddrArrSize];

  // The number of slabs allocated. Large arrays are not counted
  unsigned NumSlabs;

  // Large arrays. In SAFECode, these are currently not freed or reused. 
  // A better implementation could split them up into single slabs for reuse,
  // upon being freed.
  void *LargeArrays;

  void *prevPage[4];
  unsigned short lastUsed;

  Splay *splay;

  //  Unsigned subpool;

} PoolTy;


typedef struct MetaPoolTy {
  PoolTy *cachePool;
  hash_set<void *> *PoolTySet;
} MetaPoolTy;


extern "C" {
  void exactcheck(int a, int b) {
    if ((0 > a) || (a >= b)) {
      fprintf(stderr, "exact check failed\n");
      exit(-1);
    }
  }

  void funccheck(unsigned num, void *f, void *g, ...) {
    va_list ap;
    unsigned i = 0;
    if (f == g) return;
    i++;
    va_start(ap, g);
    for ( ; i != num; ++i) {
      void *h = va_arg(ap, void *);
      if (f == h) {
	return;
      }
    }
    abort();
  }
  
  void poolinit(PoolTy *Pool, unsigned NodeSize);
  void poolmakeunfreeable(PoolTy *Pool);
  void pooldestroy(PoolTy *Pool);
  void *poolalloc(PoolTy *Pool, unsigned NumBytes);
  void poolregister(PoolTy *Pool, unsigned NumBytes, void *allocaptr);
  void poolfree(PoolTy *Pool, void *Node);
  void poolcheck(MetaPoolTy **Pool, void *Node);
  void* poolcheckoptim(MetaPoolTy *Pool, void *Node);
  void poolstats() {
    fprintf(stderr, "pool mem usage %d\n",poolmemusage);
  }
  void poolcheckalign(PoolTy *Pool, void *Node, unsigned StartOffset, 
		 unsigned EndOffset);

  void *poolrealloc(PoolTy *Pool, void *Node, unsigned NumBytes) {
    if (Node == 0) return poolalloc(Pool, NumBytes);
    if (NumBytes == 0) {
      poolfree(Pool, Node);
      return 0;
    }
    void *New = poolalloc(Pool, NumBytes);
    //    unsigned Size =
    //FIXME the following may not work in all cases  
    memcpy(New, Node, NumBytes);
    poolfree(Pool, Node);
    return New;
  }

  void AddPoolDescToMetaPool(void **MetaPool, void *PoolDesc);

  void poolcheckarray(MetaPoolTy *Pool, void *Node, void * Node1);

}

#endif
