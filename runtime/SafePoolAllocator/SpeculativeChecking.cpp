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

// Two nodes so checks can be aggregated
struct PoolCheckRequest {
  PoolTy * Pool;
  void * Node;
  void * dummy;
};

struct BoundsCheckRequest {
  PoolTy * Pool;
  void * Source;
  void * Dest;
};

struct PoolRegisterRequest {
  PoolTy * Pool;
  void * allocaptr;
  unsigned NumBytes;
};

union CheckRequest {
    PoolCheckRequest poolcheck;
    BoundsCheckRequest boundscheck;
    PoolRegisterRequest poolregister;
    PoolRegisterRequest poolunregister;
    PoolRegisterRequest pooldestroy;
} __attribute__((packed)); 


// Seems not too many differences
//__attribute__((aligned(64)));

typedef LockFreeFifo<CheckRequest> CheckQueueTy;
CheckQueueTy gCheckQueue;

static inline void enqueueCheckRequest(const CheckRequest req, void (*op)(CheckRequest&)) {
  PROFILING(unsigned long long start_time = rdtsc();)

  gCheckQueue.enqueue(req, op);

  PROFILING(
  	unsigned long long end_time = rdtsc();
  	llvm::safecode::profile_enqueue(start_time, end_time)
		)
}

static void __stub_poolcheck(CheckRequest & req) {
	poolcheck(req.poolcheck.Pool, req.poolcheck.Node);
}

static void __stub_poolcheckui(CheckRequest & req) {
	poolcheckui(req.poolcheck.Pool, req.poolcheck.Node);
}

static void __stub_boundscheck(CheckRequest & req) {
	boundscheck(req.boundscheck.Pool, req.boundscheck.Source, req.boundscheck.Dest);
}

static void __stub_boundscheckui(CheckRequest & req) {
	boundscheckui(req.boundscheck.Pool, req.boundscheck.Source, req.boundscheck.Dest);
}

static void __stub_poolregister(CheckRequest & req) {
	poolregister(req.poolregister.Pool, req.poolregister.allocaptr, req.poolregister.NumBytes);
}

static void __stub_poolunregister(CheckRequest & req) {
	poolunregister(req.poolregister.Pool, req.poolregister.allocaptr);
}

static void __stub_pooldestroy(CheckRequest & req) {
	ParPoolAllocator::pooldestroy(req.pooldestroy.Pool);
}

static void __stub_sync(CheckRequest &) {
	gCheckingThreadWorking = false;
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
      __sc_par_wait_for_completion();
      mCheckTask.stop();
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
    CheckRequest req;
    req.poolcheck.Pool = Pool;
    req.poolcheck.Node = Node;
    enqueueCheckRequest(req, __stub_poolcheck);
  }

  void __sc_par_poolcheckui(PoolTy *Pool, void *Node) {
    CheckRequest req;
    req.poolcheck.Pool = Pool;
    req.poolcheck.Node = Node;
    enqueueCheckRequest(req, __stub_poolcheckui);
  }
  
void
__sc_par_poolcheckalign (PoolTy *Pool, void *Node, unsigned Offset) {
// FIXME: This is another type of check
// Just do pool check right now
 __sc_par_poolcheck(Pool, Node);
}

void __sc_par_boundscheck(PoolTy * Pool, void * Source, void * Dest) {
  CheckRequest req;
  req.boundscheck.Pool = Pool;
  req.boundscheck.Source = Source;
  req.boundscheck.Dest = Dest;
  enqueueCheckRequest(req, __stub_boundscheck);
}

void __sc_par_boundscheckui(PoolTy * Pool, void * Source, void * Dest) {
  CheckRequest req;
  req.boundscheck.Pool = Pool;
  req.boundscheck.Source = Source;
  req.boundscheck.Dest = Dest;
  enqueueCheckRequest(req, __stub_boundscheckui);
}

void __sc_par_poolregister(PoolTy *Pool, void *allocaptr, unsigned NumBytes){
  CheckRequest req;
  req.poolregister.Pool = Pool;
  req.poolregister.allocaptr = allocaptr;
  req.poolregister.NumBytes = NumBytes;
  enqueueCheckRequest(req, __stub_poolregister);
}

void __sc_par_poolunregister(PoolTy *Pool, void *allocaptr) {
  CheckRequest req;
  req.poolunregister.Pool = Pool;
  req.poolunregister.allocaptr = allocaptr;
  enqueueCheckRequest(req, __stub_poolunregister);
}

void __sc_par_pooldestroy(PoolTy *Pool) {
  CheckRequest req;
  req.pooldestroy.Pool = Pool;
  enqueueCheckRequest(req, __stub_pooldestroy);
}

void __sc_par_wait_for_completion() {
  PROFILING(
  unsigned int size = gCheckQueue.size();
  unsigned long long start_sync_time = rdtsc();
  )

  gCheckingThreadWorking = true;
  
  CheckRequest req;
  enqueueCheckRequest(req, __stub_sync);

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
