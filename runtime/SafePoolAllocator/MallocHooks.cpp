//===- MallocHooks.cpp - Implementation of hooks to malloc() functions ----===//
// 
//                            The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements functions that interrupt and record allocations created
// by the system's original memory allocators.  This allows the SAFECode
// compiler to work with external code.
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#include "adl_splay.h"

// Splay tree for recording external allocations
void * ExternalObjects;

#if defined(__APPLE__)
// The real allocation functions
static void * (*real_malloc)(malloc_zone_t *, size_t size);

// Prototypes for the tracking versions
static void * track_malloc (malloc_zone_t *, size_t size);

void
installAllocHooks (void) {
  // Pointer to the default malloc zone
  malloc_zone_t * default_zone;

  //
  // Get the default malloc zone and record the pointers to the real malloc
  // functions.
  //
  default_zone = malloc_default_zone();
  real_malloc = default_zone->malloc;

  //
  // Install intercept routines.
  //
  default_zone->malloc = track_malloc;
}

static void *
track_malloc (malloc_zone_t * zone, size_t size) {
  // Pointer to the allocated object
  void * objp;

  //
  // Perform the allocation.
  //
  objp = real_malloc (zone, size);

  //
  // Record the allocation and return to the caller.
  //
  adl_splay_insert (&(ExternalObjects), objp, size, 0);
  return objp;
}
#else
void
installAllocHooks (void) {
  return;
}
#endif
