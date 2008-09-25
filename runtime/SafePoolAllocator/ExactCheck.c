/*===- ExactCheck.c - Implementation of exactcheck functions --------------===*/
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

#include "ExactCheck.h"
#include "Report.h"

#ifdef LLVA_KERNEL
#include <stdarg.h>
#else
#include <stdio.h>
#endif
#define DEBUG(x) 

/* Decleare this structure type */
struct PoolTy;

/* Function to rewriting pointers to Out Of Bounds (OOB) Pointers */
extern void * rewrite_ptr (struct PoolTy * P, void * p);

/*
 * Function: exactcheck()
 *
 * Description:
 *  Determine whether the index is within the specified bounds.
 *
 * Inputs:
 *  a      - The index given as an integer.
 *  b      - The index of one past the end of the array.
 *  result - The pointer that is being checked.
 *
 * Return value:
 *  If there is no bounds check violation, the result pointer is returned.
 *  This forces the call to exactcheck() to be considered live (previous
 *  optimizations dead-code eliminated it).
 */
void *
exactcheck (int a, int b, void * result) {
  if ((0 > a) || (a >= b)) {
    poolcheckfail ("exact check failed", (a), (void*)__builtin_return_address(0));
    poolcheckfail ("exact check failed", (b), (void*)__builtin_return_address(0));
  }
  return result;
}

void *
exactcheck2 (signed char *base, signed char *result, unsigned size) {
  if ((result < base) || (result >= (base + size)))
#if 1
    return rewrite_ptr (0, result);
#else
  {
    ReportExactCheck ((unsigned)0xbeefdeed,
                      (unsigned)result,
                      (unsigned)__builtin_return_address(0),
                      (unsigned)base,
                      (unsigned)size);
  }
#endif
  return result;
}

void *
exactcheck2a (signed char *base, signed char *result, unsigned size) {
  if (result >= base + size ) {
    ReportExactCheck ((unsigned)0xbeefdeed,
                      (unsigned)result,
                      (unsigned)__builtin_return_address(0),
                      (unsigned)base,
                      (unsigned)size);
  }
  return result;
}

void *
exactcheck3(signed char *base, signed char *result, signed char * end) {
  if ((result < base) || (result > end )) {
    ReportExactCheck ((unsigned)0xbeefbeef,
                      (unsigned)result,
                      (unsigned)__builtin_return_address(0),
                      (unsigned)base,
                      (unsigned)(end-base));
  }
  return result;
}

