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

template<class T> class LockFreeFifo
{
  static const size_t N = 65536;
public:
  typedef void (*ptr_t)(T&);
  struct element_t {
    ptr_t op;
    T val;
  };

  LockFreeFifo () {
    readidx = writeidx = 0;
    memset(&buffer[0], sizeof(buffer), 0);
  }

  inline void dispatch (void) {
    unsigned val = readidx;
    while (!buffer[val].op) {mb();}
    buffer[val].op(buffer[val].val);
    buffer[val].op = 0;
    mb();
    readidx = (val + 1) % N;
  }

  inline void enqueue (T datum, ptr_t op)
  {
    unsigned val = writeidx;
    while (buffer[val].op) {mb();}
    buffer[val].val = datum;
    mb();
    buffer[val].op = op;
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
  element_t __attribute__((aligned(128))) buffer[N];
};

template <class QueueTy>
class Task {
public:
  Task(QueueTy & queue) : mQueue(queue), mActive(false) {}
  void activate() {
    mActive = true;
    typedef void * (*start_routine_t)(void*);
    pthread_t thr;
    pthread_create(&thr, NULL, (start_routine_t)(&Task::runHelper), this);
  }
  
  void stop() {
    mActive = false;
  }

  QueueTy & getQueue() const {
    return mQueue;
  }

private:
  static void * runHelper(Task * this_) {
    this_->run();
    return NULL;
  }

  void run() {
    while(mActive)
      mQueue.dispatch();
  }
  
  QueueTy & mQueue;
  bool mActive;
};

NAMESPACE_SC_END

#endif
