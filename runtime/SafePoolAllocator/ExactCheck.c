/*===- ExactCheck.c - Implementation of exactcheck functions --------------===*/
/*                                                                            */
/*                          The SAFECode Compiler                             */
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

#include "ConfigData.h"
#include "ExactCheck.h"
#include "Report.h"

#include "safecode/Config/config.h"

#ifdef LLVA_KERNEL
#include <stdarg.h>
#else
#include <stdio.h>
#endif
#define DEBUG(x) 


/* Decleare this structure type */
struct PoolTy;

/* Function to rewriting pointers to Out Of Bounds (OOB) Pointers */
extern void * rewrite_ptr (struct PoolTy * P, void * p, void * SF, unsigned l);

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
    ReportExactCheck ((unsigned)0xbeefdeed,
                      (uintptr_t)result,
                      (uintptr_t)__builtin_return_address(0),
                      (unsigned)a,
                      (unsigned)0,
                      "<Unknown>",
                      0);
#if 0
    poolcheckfail ("exact check failed", (a), (void*)__builtin_return_address(0));
    poolcheckfail ("exact check failed", (b), (void*)__builtin_return_address(0));
#endif
  }
  return result;
}

/*
 * Function: exactcheck2()
 *
 * Description:
 *  Determine whether a pointer is within the specified bounds of an object.
 *
 * Inputs:
 *  base   - The address of the first byte of a memory object.
 *  result - The pointer that is being checked.
 *  size   - The size of the object in bytes.
 *
 * Return value:
 *  If there is no bounds check violation, the result pointer is returned.
 *  Otherwise, depending upon the configuration of the run-time, either an
 *  error is returned or a rewritten Out-of-Bounds (OOB) pointer is returned.
 */
void *
exactcheck2 (signed char *base, signed char *result, unsigned size) {
  if ((result < base) || (result >= (base + size))) {
    if (ConfigData.StrictIndexing)
      ReportExactCheck ((unsigned)0xbeefdeed,
                        (uintptr_t)result,
                        (uintptr_t)__builtin_return_address(0),
                        (uintptr_t)base,
                        (unsigned)size,
                        "<Unknown>",
                        0);
    else
      return rewrite_ptr (0, result, "<Unknown>", 0);
  }

  return result;
}

/*
 * Function: exactcheck2_debug()
 *
 * Description:
 *  This function is identical to exactcheck2(), but the caller provides more
 *  source level information about the run-time check for error reporting if
 *  the check fails.
 *
 * Inputs:
 *  base   - The address of the first byte of a memory object.
 *  result - The pointer that is being checked.
 *  size   - The size of the object in bytes.
 *
 * Return value:
 *  If there is no bounds check violation, the result pointer is returned.
 *  This forces the call to exactcheck() to be considered live (previous
 *  optimizations dead-code eliminated it).
 */
void *
exactcheck2_debug (signed char *base,
                   signed char *result,
                   unsigned size,
                   void * SourceFile,
                   unsigned lineno) {
  if ((result < base) || (result >= (base + size))) {
    if (ConfigData.StrictIndexing)
      ReportExactCheck ((unsigned)0xbeefdeed,
                        (uintptr_t)result,
                        (uintptr_t)__builtin_return_address(0),
                        (uintptr_t)base,
                        (unsigned)size,
                        (unsigned char *)(SourceFile),
                        lineno);
    else
      return rewrite_ptr (0, result, SourceFile, lineno);
  }

  return result;
}

void *
exactcheck2a (signed char *base, signed char *result, unsigned size) {
  if (result >= base + size ) {
    ReportExactCheck ((unsigned)0xbeefdeed,
                      (uintptr_t)result,
                      (uintptr_t)__builtin_return_address(0),
                      (uintptr_t)base,
                      (unsigned)size,
                      "<Unknown>",
                      0);
  }
  return result;
}

void *
exactcheck3(signed char *base, signed char *result, signed char * end) {
  if ((result < base) || (result > end )) {
    ReportExactCheck ((unsigned)0xbeefbeef,
                      (uintptr_t)result,
                      (uintptr_t)__builtin_return_address(0),
                      (uintptr_t)base,
                      (unsigned)(end-base),
                      "<Unknown>",
                      0);
  }
  return result;
}

