//===------- Gets.cpp - CStdLib Runtime functions for the gets()/fgets() --===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file provides CStdLib runtime wrapper versions of fgets().
//
//===----------------------------------------------------------------------===//

#include <cstdio>

#include "safecode/Config/config.h"
#include "CStdLib.h"

//
// Function: pool_fgets()
//
// Description:
//  This is a memory safe replacement for the fgets() function.
//
// Inputs:
//   Pool     - The pool handle for the string to write.
//   s        - The memory buffer into which to read the result.
//   n        - The maximum number of bytes to read.
//   stream   - The FILE * from which to read the data.
//   complete - The Completeness bit vector.
//   TAG      - The Tag information for debugging purposes
//   SRC_INFO - Source file and line number information for debugging purposes
//
char *
pool_fgets_debug (DebugPoolTy * Pool,
                  char * s,
                  int n,
                  FILE * stream,
                  const uint8_t complete,
                  TAG,
                  SRC_INFO) {
  //
  // Determine the completeness of the various pointers.
  //
  const bool BufferComplete = ARG1_COMPLETE(complete);

  //
  // Retrieve the buffer's bounds from the pool.  If we cannot find the object
  // and we know everything about what the buffer should be pointing to (i.e.,
  // the check is complete), then report an error.
  //
  bool found;
  void * ObjStart;
  void * ObjEnd;
  if (!(found = pool_find (Pool, s, ObjStart, ObjEnd)) && BufferComplete) {
    LOAD_STORE_VIOLATION (s, Pool, SRC_INFO_ARGS);
  }

  //
  // Calculate a new length based on the space available in the memory object.
  //
  int length;
  int remaining = (char *)(ObjEnd) - s + 1;
  if (remaining < n)
    length = remaining;
  else
    length = n;

  //
  // Call the real fgets() function and return what it returns.
  //
  return fgets (s, length, stream);
}

char *
pool_fgets (DebugPoolTy * Pool,
            char * s,
            int n,
            FILE * stream,
            const uint8_t complete) {
  return pool_fgets_debug (Pool, s, n, stream, complete, DEFAULTS);
}

