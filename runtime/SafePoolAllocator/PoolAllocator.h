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
#include "llvm/ADT/hash_set"

#include <stdarg.h>

#include <map>
#include <utility>

#define AddrArrSize 2
extern unsigned poolmemusage;
//unsigned PCheckPassed = 1;
typedef struct PoolTy {
  // Splay tree used for object registration
  void * Objects;
  
  // Splay tree used by dangling pointer runtime
  void * DPTree;

  // Splay tree used for out of bound objects
  void * OOB;

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
  void *FreeLargeArrays;

  void *prevPage[4];
  unsigned short lastUsed;

  short AllocadPool;
  void *allocaptr;

  std::map<void *,unsigned> * RegNodes;
} PoolTy;

typedef struct DebugMetaData {
  unsigned allocID;
  unsigned freeID;
  void * allocPC;
  void * freePC;
  void * canonAddr;
} DebugMetaData;

typedef DebugMetaData * PDebugMetaData;

extern "C" {
  static void exactcheck(int a, int b) {
    if ((0 > a) || (a >= b)) {
      fprintf(stderr, "exact check failed\n");
      exit(-1);
    }
  }
  void pool_init_runtime(unsigned Dangling);
  void poolinit(PoolTy *Pool, unsigned NodeSize);
  void poolmakeunfreeable(PoolTy *Pool);
  void pooldestroy(PoolTy *Pool);
  void * poolalloc(PoolTy *Pool, unsigned NumBytes);
  void * poolrealloc(PoolTy *Pool, void *Node, unsigned NumBytes);
  void * poolcalloc (PoolTy *Pool, unsigned Number, unsigned NumBytes);
  void * poolstrdup(PoolTy *Pool, char *Node);

  void poolregister(PoolTy *Pool, void *allocaptr, unsigned NumBytes);
  void poolunregister(PoolTy *Pool, void *allocaptr);
  void poolfree(PoolTy *Pool, void *Node);
  void poolcheck(PoolTy *Pool, void *Node);
  void poolcheckui(PoolTy *Pool, void *Node);
  void poolcheckoptim(PoolTy *Pool, void *Node);
  void * boundscheck   (PoolTy * Pool, void * Source, void * Dest);
  void * boundscheckui (PoolTy * Pool, void * Source, void * Dest);
  void funccheck (unsigned num, void *f, void *g, ...);
  void poolstats(void);
  void poolcheckalign(PoolTy *Pool, void *Node, unsigned StartOffset, 
                      unsigned EndOffset);
  //void protect_shadowpage();
}

#endif
