/*===- ExactCheck.cpp - Implementation of exactcheck functions ------------===*/
/*                                                                            */
/*                       The LLVM Compiler Infrastructure                     */
/*                                                                            */
/* This file was developed by the LLVM research group and is distributed      */
/* under the University of Illinois Open Source License. See LICENSE.TXT for  */
/* details.                                                                   */
/*                                                                            */
/*===----------------------------------------------------------------------===*/
/*                                                                            */
/* This file implements the exactcheck family of functions.                   */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

#include "PoolCheck.h"
#include "PoolSystem.h"
#include "adl_splay.h"
#ifdef LLVA_KERNEL
#include <stdarg.h>
#endif
#define DEBUG(x) 

/* Flag whether to print error messages on bounds violations */
int ec_do_fail = 0;

/* Flags whether we're ready to do run-time checks */
extern int pchk_ready;

extern int stat_exactcheck;
extern int stat_exactcheck2;
extern int stat_exactcheck3;

extern int stat_poolcheck;

/* Global splay for holding the integer states */
extern MetaPoolTy IntegerStatePool;

void * exactcheck(int a, int b, void * result) {
  ++stat_exactcheck;
  if ((0 > a) || (a >= b)) {
    if(ec_do_fail) poolcheckfail ("exact check failed", (a), (void*)__builtin_return_address(0));
    if(ec_do_fail) poolcheckfail ("exact check failed", (b), (void*)__builtin_return_address(0));
  }
  return result;
}

void * exactcheck2(signed char *base, signed char *result, unsigned size) {
  ++stat_exactcheck2;
  if ((result < base) || (result >= base + size )) {
    if (ec_do_fail) poolcheckfail("exactcheck2: ", (unsigned)base, (void*)__builtin_return_address(0));
    if (ec_do_fail) poolcheckfail("exactcheck2: ", (unsigned)result, (void*)__builtin_return_address(0));
    if (ec_do_fail) poolcheckfail("exactcheck2: ", (unsigned)size, (void*)__builtin_return_address(0));
  }
  return result;
}

void * exactcheck2a(signed char *base, signed char *result, unsigned size) {
  ++stat_exactcheck2;
  if (result >= base + size ) {
    if (ec_do_fail) poolcheckfail("exactcheck2a: ", (unsigned)base, (void*)__builtin_return_address(0));
    if (ec_do_fail) poolcheckfail("exactcheck2a: ", (unsigned)result, (void*)__builtin_return_address(0));
    if (ec_do_fail) poolcheckfail("exactcheck2a: ", (unsigned)size, (void*)__builtin_return_address(0));
  }
  return result;
}

void *
exactcheck3(signed char *base, signed char *result, signed char * end) {
  ++stat_exactcheck3;
  if ((result < base) || (result > end )) {
    if (ec_do_fail) {
#if 0
      poolcheckfail("Array bounds violation detected ", (unsigned)base, (void*)__builtin_return_address(0));
#else
      poolcheckfail("exactcheck3: ", (unsigned)base, (void*)__builtin_return_address(0));
      poolcheckfail("exactcheck3: ", (unsigned)result, (void*)__builtin_return_address(0));
      poolcheckfail("exactcheck3: ", (unsigned)end, (void*)__builtin_return_address(0));
    }
#endif
  }

  /*
   * Ensure that the result is not within an Integer State.
   */
#ifdef SVA_KSTACKS
  if (pchk_check_int (result))
    poolcheckfail ("Pointer within Integer State detected ", (unsigned)base, (void*)__builtin_return_address(0));
#endif
  return result;
}

void funccheck (unsigned num, void *f, void *t1, void *t2, void *t3,
                                       void *t4, void *t5, void *t6) {
  if ((t1) && (f == t1)) return;
  if ((t2) && (f == t2)) return;
  if ((t3) && (f == t3)) return;
  if ((t4) && (f == t4)) return;
  if ((t5) && (f == t5)) return;
  if ((t6) && (f == t6)) return;
  if (ec_do_fail) poolcheckfail ("funccheck failed", f, (void*)__builtin_return_address(0));
  return;
}

void funccheck_t (unsigned num, void * f, void ** table) {
  unsigned int index;
  /*
   * Look for the pointer in the big table
   */
  for (index = 0; index < num; ++index) {
    if (f == table[index]) {
      return;
    }
  }

  if (ec_do_fail) poolcheckfail ("funccheck_t failed", f, (void*)__builtin_return_address(0));
}

struct node {
  void* left;
  void* right;
  char* key;
  char* end;
  void* tag;
};

void * getBegin (void * node) {
  return ((struct node *)(node))->key;
}

void * getEnd (void * node) {
  return ((struct node *)(node))->end;
}

