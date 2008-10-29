//===- SpeculativeChecking.cpp - Implementation of Speculative Checking --*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the asynchronous checking interfaces, enqueues checking requests
// and provides a synchronization token for each checking request.
//
//===----------------------------------------------------------------------===//

#include "Config.h"
#include "SafeCodeRuntime.h"
#include "PoolAllocator.h"
#include "AtomicOps.h"
#include "Profiler.h"
#include "ParPoolAllocator.h"
#include <iostream>

NAMESPACE_SC_BEGIN

static unsigned int gDataStart;
static unsigned int gDataEnd;

// A flag to indicate that the checking thread has done its work
static volatile unsigned int __attribute__((aligned(128))) gCheckingThreadWorking = 0;

typedef LockFreeFifo CheckQueueTy;
CheckQueueTy gCheckQueue;

static void __stub_poolcheck(uintptr_t* req) {
  poolcheck((PoolTy*)req[0], (void*)req[1]);
}

static void __stub_poolcheckui(uintptr_t* req) {
  poolcheckui((PoolTy*)req[0], (void*)req[1]);
}

static void __stub_boundscheck(uintptr_t* req) {
  boundscheck((PoolTy*)req[0], (void*)req[1], (void*)req[2]);
}

static void __stub_boundscheckui(uintptr_t* req) {
  boundscheckui((PoolTy*)req[0], (void*)req[1], (void*)req[2]);
}

static void __stub_poolregister(uintptr_t* req) {
  poolregister((PoolTy*)req[0], (void*)req[1], req[2]);
}

static void __stub_poolunregister(uintptr_t* req) {
  poolunregister((PoolTy*)req[0], (void*)req[1]);
}

static void __stub_pooldestroy(uintptr_t* req) {
  ParPoolAllocator::pooldestroy((PoolTy*)req[0]);
}

static void __stub_sync(uintptr_t* ) {
  gCheckingThreadWorking = false;
}

static void __stub_stop(uintptr_t*) {
  pthread_exit(NULL);
}

extern "C" {
void __sc_par_wait_for_completion();
}

namespace {
  class SpeculativeCheckingGuard {
  public:
    SpeculativeCheckingGuard() : mCheckTask(gCheckQueue) {
    }
    ~SpeculativeCheckingGuard() {
        gCheckQueue.enqueue(__stub_stop);
        pthread_join(mCheckTask.thread(), NULL);
    }

    void activate(void) {
      mCheckTask.activate();
    }

  private:
    Task<CheckQueueTy> mCheckTask;
  };
}


NAMESPACE_SC_END

using namespace llvm::safecode;

extern "C" {
  void __sc_par_poolcheck(PoolTy *Pool, void *Node) {
    gCheckQueue.enqueue((uintptr_t)Pool, (uintptr_t)Node, __stub_poolcheck);
  }

  void __sc_par_poolcheckui(PoolTy *Pool, void *Node) {
    gCheckQueue.enqueue((uintptr_t)Pool, (uintptr_t)Node, __stub_poolcheckui);
  }
  
void
__sc_par_poolcheckalign (PoolTy *Pool, void *Node, unsigned Offset) {
// FIXME: This is another type of check
// Just do pool check right now
 __sc_par_poolcheck(Pool, Node);
}

void __sc_par_boundscheck(PoolTy * Pool, void * Source, void * Dest) {
  gCheckQueue.enqueue ((uintptr_t)Pool, (uintptr_t)Source, (uintptr_t)Dest, __stub_boundscheck);
}

void __sc_par_boundscheckui(PoolTy * Pool, void * Source, void * Dest) {
  gCheckQueue.enqueue((uintptr_t)Pool, (uintptr_t)Source, (uintptr_t)Dest, __stub_boundscheckui);
}

void __sc_par_poolregister(PoolTy *Pool, void *allocaptr, unsigned NumBytes){
  gCheckQueue.enqueue((uintptr_t)Pool, (uintptr_t)allocaptr, NumBytes, __stub_poolregister);
}

void __sc_par_poolunregister(PoolTy *Pool, void *allocaptr) {
  gCheckQueue.enqueue((uintptr_t)Pool, (uintptr_t)allocaptr, __stub_poolunregister);
}

void __sc_par_pooldestroy(PoolTy *Pool) {
  gCheckQueue.enqueue((uintptr_t)Pool, __stub_pooldestroy);
}

void __sc_par_wait_for_completion() {
  PROFILING(
  unsigned int size = gCheckQueue.size();
  unsigned long long start_sync_time = rdtsc();
  )

  gCheckingThreadWorking = true;
  
  gCheckQueue.enqueue(__stub_sync);

  while (gCheckingThreadWorking) {}

  PROFILING(
  unsigned long long end_sync_time = rdtsc();
  llvm::safecode::profile_sync_point(start_sync_time, end_sync_time, size);
  )
}

void __sc_par_store_check(void * ptr) {
  if (&gDataStart <= ptr && ptr <= &gDataEnd) {
    __builtin_trap();
  }
} 

void __sc_par_init_runtime(void) {
  static SpeculativeCheckingGuard g;
  g.activate();
}

}
