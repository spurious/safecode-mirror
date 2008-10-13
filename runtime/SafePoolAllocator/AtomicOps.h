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

NAMESPACE_SC_BEGIN

#define LOCK_PREFIX "lock "
#define ADDR "+m" (*(volatile long *) addr)

#define SPIN_AND_YIELD(COND) do { unsigned short counter = 0; \
  while (COND) { if (++counter == 0) {                      \
    sched_yield();                                        \
/*    fprintf(stderr, "yielding: %s\n", #COND); fflush(stderr); */}}       \
} while (0)

/// FIXME: These codes are from linux header file, it should be rewritten
/// to avoid license issues.
/// JUST FOR EXPERIMENTAL USE!!!

static inline void clear_bit(int nr, volatile void *addr)
{
  asm volatile(LOCK_PREFIX "btr %1,%0"
	       : ADDR
	       : "Ir" (nr));
}
static inline void set_bit(int nr, volatile void *addr)
{
  asm volatile(LOCK_PREFIX "bts %1,%0"
	       : ADDR
	       : "Ir" (nr) : "memory");
}
/**

* __ffs - find first bit in word.
* @word: The word to search
*
* Undefined if no bit exists, so code should check against 0 first.
*/
static inline unsigned long __ffs(unsigned long word)
{
  __asm__( "bsfl %1,%0"
	  :"=r" (word)
	  :"rm" (word));
  return word;
}

struct __xchg_dummy {
	unsigned long a[100];
};

#define __xg(x) ((struct __xchg_dummy *)(x))


static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long newval, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		asm volatile(LOCK_PREFIX "cmpxchgb %b1,%2"
			     : "=a"(prev)
			     : "q"(newval), "m"(*__xg(ptr)), "0"(old)
			     : "memory");
		return prev;
	case 2:
		asm volatile(LOCK_PREFIX "cmpxchgw %w1,%2"
			     : "=a"(prev)
			     : "r"(newval), "m"(*__xg(ptr)), "0"(old)
			     : "memory");
		return prev;
	case 4:
		asm volatile(LOCK_PREFIX "cmpxchgl %1,%2"
			     : "=a"(prev)
			     : "r"(newval), "m"(*__xg(ptr)), "0"(old)
			     : "memory");
		return prev;
	}
	return old;
}

/* Copied from include/asm-x86_64 for use by userspace. */
//#define mb()    asm volatile("mfence":::"memory")
#define mb()  asm volatile ("" ::: "memory")

/// A very simple allocator works on single-reader / single-writer cases
/// Based on
/// http://www.talkaboutprogramming.com/group/comp.programming.threads/messages/40308.html
/// Only for single-reader, single-writer cases.

template<class T> class LockFreeFifo
{
  static const int N = 65536;
public:
  typedef  T element_t;
  LockFreeFifo () : readidx(0), writeidx(0) {
    for(int x = 0; x < N; ++x)
      buffer[x].set_free();
  }

  T & front (void) {
    SPIN_AND_YIELD(buffer[readidx].is_free());
    return buffer[readidx];
  }

  void dequeue (void)
  {
    // CAUTION: always supposes the queue is not empty.
    //    SPIN_AND_YIELD(empty());
    buffer[readidx].set_free();
    // Use overflow to wrap the queue
    ++readidx;
    //    asm volatile (LOCK_PREFIX "incb %0" : "+m" (readidx));
  }

  template <class U>
  void enqueue (T & datum, U type)
  {
    SPIN_AND_YIELD(!buffer[writeidx].is_free());
    buffer[writeidx] = datum;
    mb();
    buffer[writeidx].set_type(type);
    ++writeidx;
  }

  bool empty() const {
    return readidx == writeidx;
  }

  unsigned size() const {
    unsigned short read = readidx;
    unsigned short write = writeidx;
    if (write >= read) return write - read;
    else return N - (read - write);
  }

private:

  // Cache alignment suggested by Andrew
  volatile unsigned short __attribute__((aligned(128))) dummy1;
  volatile unsigned short __attribute__((aligned(128))) readidx;
  volatile unsigned short __attribute__((aligned(128))) writeidx;
  volatile unsigned short __attribute__((aligned(128))) dummy2; 
  T buffer[N];


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
