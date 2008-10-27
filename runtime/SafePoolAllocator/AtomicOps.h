//=== AtomicOps.h --- Declare atomic operation primitives -------*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file declares synchronization primitives used in speculative checking.
//
//===----------------------------------------------------------------------===//

#ifndef _ATOMIC_OPS_H_
#define _ATOMIC_OPS_H_

#include <pthread.h>
#include <cassert>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include "Config.h"
#include "Profiler.h"

NAMESPACE_SC_BEGIN

#define mb()  asm volatile ("" ::: "memory")

/// A very simple allocator works on single-reader / single-writer cases
/// Based on
/// http://www.talkaboutprogramming.com/group/comp.programming.threads/messages/40308.html
/// Only for single-reader, single-writer cases.

#define INLINE1 inline
#define INLINE2 __attribute__((always_inline))
//#define INLINE1
//#define INLINE2

template<class T> class LockFreeFifo
{
  static const size_t N = 65536;
public:
  typedef  T element_t;
  LockFreeFifo () {
    readidx = writeidx = 0;
    for(size_t x = 0; x < N; ++x)
      buffer[x].set_free();
  }

  INLINE1 T & INLINE2 front (void) {
    unsigned val = readidx;
    while (buffer[val].is_free()) {mb();}
    return buffer[val];
  }

  INLINE1 void INLINE2 dequeue (void)
  {
    // CAUTION: always supposes the queue is not empty.
    unsigned val = readidx;
    buffer[val].set_free();
    mb();
    readidx = (val + 1) % N;
  }

  template <class U>
  INLINE1 void INLINE2 enqueue (T datum, U type)
  {
    unsigned val = writeidx;
    while (!buffer[val].is_free()) {mb();}
    buffer[val] = datum;
    mb();
    buffer[val].set_type(type);
    mb();
    writeidx = (val + 1) % N;
  }

  inline bool empty() const {
    return readidx == writeidx;
  }

  inline unsigned size() const {
    unsigned read = readidx;
    unsigned write = writeidx;
    if (write >= read) return write - read;
    else return N - (read - write);
  }

private:
  volatile unsigned __attribute__((aligned(128))) readidx;
  volatile unsigned __attribute__((aligned(128))) writeidx;
  T __attribute__((aligned(128))) buffer[N];
};

template <class QueueTy, class FuncTy>
class Task {
public:
  typedef typename QueueTy::element_t ElemTy;
  Task(QueueTy & queue) : mQueue(queue), mActive(false) {}
  void activate() {
    mActive = true;
    typedef void * (*start_routine_t)(void*);
    pthread_t thr;
    pthread_create(&thr, NULL, (start_routine_t)(&Task::runHelper), this);
//    fprintf(stderr, "pthread: create thr %d\n", thr);
  };

  void stop() {
    mActive = false;
  };

  QueueTy & getQueue() const {
    return mQueue;
  };

private:
  static void * runHelper(Task * this_) {
    this_->run();
    return NULL;
  };

  void run() {
    while(true) {
      typename QueueTy::element_t & e = mQueue.front();
      if (mActive) {
        mFunctor(e);
        mQueue.dequeue();
      } else {
        return;
      }
    }
  };

  QueueTy & mQueue;
  FuncTy mFunctor;
  bool mActive;
};

NAMESPACE_SC_END

#endif
