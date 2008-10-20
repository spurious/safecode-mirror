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

#ifndef ENABLE_PROFILING
#define ENQUEUE_CHECK_REQUEST(REQ, TYPE) \
  gCheckQueue.enqueue(REQ, TYPE)
#else
#define ENQUEUE_CHECK_REQUEST(REQ, TYPE) \
  unsigned long long start_time = rdtsc(); \
  gCheckQueue.enqueue(REQ, TYPE); \
  unsigned long long end_time = rdtsc(); \
  llvm::safecode::profile_enqueue(start_time, end_time)
#endif

NAMESPACE_SC_BEGIN

static unsigned int gDataStart;
static unsigned int gDataEnd;

// A flag to indicate that the checking thread has done its work
static unsigned int __attribute__((aligned(128))) gCheckingThreadWorking = 0;

struct PoolCheckRequest {
  PoolTy * Pool;
  void * Node;
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

typedef enum {
  CHECK_EMPTY = 0,
  CHECK_POOL_CHECK,
  CHECK_POOL_CHECK_UI,
  CHECK_BOUNDS_CHECK,
  CHECK_BOUNDS_CHECK_UI,
  CHECK_POOL_REGISTER,
  CHECK_POOL_UNREGISTER,
  CHECK_POOL_DESTROY,
  CHECK_SYNC,
  CHECK_REQUEST_COUNT
} RequestTy;

struct CheckRequest {
  RequestTy type;
  union {
    PoolCheckRequest poolcheck;
    BoundsCheckRequest boundscheck;
    PoolRegisterRequest poolregister;
    PoolRegisterRequest poolunregister;
    PoolRegisterRequest pooldestroy;
  };

  bool is_free() const {
    return type == CHECK_EMPTY;
  }
  void set_type(RequestTy t) {
    type = t;
  }
  void set_free() {
    type = CHECK_EMPTY;
  }
} __attribute__((packed)); 

// Seems not too many differences
//__attribute__((aligned(64)));

typedef LockFreeFifo<CheckRequest> CheckQueueTy;
CheckQueueTy gCheckQueue;
  
class CheckWrapper {
public:
  void operator()(CheckRequest & req) const {
    PROFILING (unsigned long long start_time = rdtsc();)
      switch (req.type) {
      case CHECK_POOL_CHECK:
	poolcheck(req.poolcheck.Pool, req.poolcheck.Node);
	break;

      case CHECK_POOL_CHECK_UI:
	poolcheckui(req.poolcheck.Pool, req.poolcheck.Node);
	break;

      case CHECK_BOUNDS_CHECK:
	boundscheck(req.boundscheck.Pool, req.boundscheck.Source, req.boundscheck.Dest);
	break;

      case CHECK_BOUNDS_CHECK_UI:
	boundscheckui(req.boundscheck.Pool, req.boundscheck.Source, req.boundscheck.Dest);
	break;

      case CHECK_POOL_REGISTER:
	poolregister(req.poolregister.Pool, req.poolregister.allocaptr, req.poolregister.NumBytes);
	break;
    
      case CHECK_POOL_UNREGISTER:
	poolunregister(req.poolregister.Pool, req.poolregister.allocaptr);
	break;

      case CHECK_POOL_DESTROY:
	ParPoolAllocator::pooldestroy(req.pooldestroy.Pool);
	break;

      case CHECK_SYNC:
	gCheckingThreadWorking = false;
	break;

      default:
	assert(0 && "Error Type!");
	break;
      }
    
    PROFILING (
	       unsigned long long end_time = rdtsc();
	       llvm::safecode::profile_queue_op(req.type, start_time, end_time);
	       ) 
      }
};


namespace {
  class SpeculativeCheckingGuard {
  public:
    SpeculativeCheckingGuard() : mCheckTask(gCheckQueue) {
      mCheckTask.activate();
    }
    ~SpeculativeCheckingGuard() {
      mCheckTask.stop();
//      CheckRequest req;
//      req.type = CHECK_REQUEST_COUNT;
//      gCheckQueue.enqueue(req);
      // Since the whole program stops, just skip the undone checks..
//      __sc_par_wait_for_completion();
    }
  private:
    Task<CheckQueueTy, CheckWrapper> mCheckTask;
  };
  SpeculativeCheckingGuard g;
}


NAMESPACE_SC_END

using namespace llvm::safecode;

extern "C" {
void __sc_par_poolcheck(PoolTy *Pool, void *Node) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.poolcheck.Pool = Pool;
  req.poolcheck.Node = Node;
  ENQUEUE_CHECK_REQUEST(req, CHECK_POOL_CHECK);
}

void __sc_par_poolcheckui(PoolTy *Pool, void *Node) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.poolcheck.Pool = Pool;
  req.poolcheck.Node = Node;
  ENQUEUE_CHECK_REQUEST(req, CHECK_POOL_CHECK_UI);
}

void
__sc_par_poolcheckalign (PoolTy *Pool, void *Node, unsigned Offset) {
// FIXME: This is another type of check
// Just do pool check right now
 __sc_par_poolcheck(Pool, Node);
}

void __sc_par_boundscheck(PoolTy * Pool, void * Source, void * Dest) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.boundscheck.Pool = Pool;
  req.boundscheck.Source = Source;
  req.boundscheck.Dest = Dest;
  ENQUEUE_CHECK_REQUEST(req, CHECK_BOUNDS_CHECK);
}

void __sc_par_boundscheckui(PoolTy * Pool, void * Source, void * Dest) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.boundscheck.Pool = Pool;
  req.boundscheck.Source = Source;
  req.boundscheck.Dest = Dest;
  ENQUEUE_CHECK_REQUEST(req, CHECK_BOUNDS_CHECK_UI);
}

void __sc_par_poolregister(PoolTy *Pool, void *allocaptr, unsigned NumBytes){
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.poolregister.Pool = Pool;
  req.poolregister.allocaptr = allocaptr;
  req.poolregister.NumBytes = NumBytes;
  ENQUEUE_CHECK_REQUEST(req, CHECK_POOL_REGISTER);
}

void __sc_par_poolunregister(PoolTy *Pool, void *allocaptr) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.poolunregister.Pool = Pool;
  req.poolunregister.allocaptr = allocaptr;
  ENQUEUE_CHECK_REQUEST(req, CHECK_POOL_UNREGISTER);
}

void __sc_par_pooldestroy(PoolTy *Pool) {
  CheckRequest req;
  req.type = CHECK_EMPTY;
  req.pooldestroy.Pool = Pool;
  ENQUEUE_CHECK_REQUEST(req, CHECK_POOL_DESTROY);
}

void __sc_par_wait_for_completion() {
  PROFILING(
  unsigned int size = gCheckQueue.size();
  unsigned long long start_sync_time = rdtsc();
  )

  //  SPIN_AND_YIELD(!gCheckQueue.empty());

  gCheckingThreadWorking = true;
  
  CheckRequest req;
  req.type = CHECK_EMPTY;
  ENQUEUE_CHECK_REQUEST(req, CHECK_SYNC);

  SPIN_AND_YIELD(gCheckingThreadWorking);

  PROFILING(
  unsigned long long end_sync_time = rdtsc();
  llvm::safecode::profile_sync_point(start_sync_time, end_sync_time, size);
  )
}

void __sc_par_store_check(void * ptr) {
  if (&gDataStart <= ptr && ptr <= &gDataEnd) {
     *((volatile int*)0);
  }
} 

}
