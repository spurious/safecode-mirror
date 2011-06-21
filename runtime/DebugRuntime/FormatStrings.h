//===- FormatStrings.h - Header for the format string function runtime ----===//
// 
//                            The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file contains definitions of structures and functions used by the
// format string functions in the runtime.
//
//===----------------------------------------------------------------------===//

#ifndef _FORMAT_STRING_RUNTIME_H
#define _FORMAT_STRING_RUNTIME_H

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include <iostream>

#include "PoolAllocator.h"

//
// The pointer_info structure and associated flags
//
#define ISCOMPLETE  0x01 // Whether the pointer is complete according to DSA
#define ISRETRIEVED 0x02 // Whether there has been an attempt made to retrive
                         // the target object's boundaries
#define HAVEBOUNDS  0x04 // Whether the boundaries were retrieved successfully

typedef struct
{
  void *ptr;
  void *pool;
  void *bounds[2];
  uint8_t flags;
} pointer_info;

typedef struct
{
  uint32_t vargc;
  void *whitelist[1];
} call_info;

typedef struct
{
  enum
  {
    OUTPUT_TO_ALLOCATED_STRING,
    OUTPUT_TO_STRING,
    OUTPUT_TO_FILE
  } OutputKind;
  union
  {
    FILE *File;
    struct
    {
      char   *string;
      size_t pos;
      size_t maxsz;
    } String;
  } Output;
} output_parameter;

#define USE_M_DIRECTIVE 0x0001 // Enable parsing of the %m directive for
                               // syslog()
typedef unsigned options_t;

// Error reporting functions
extern void out_of_bounds_error(pointer_info *p, size_t obj_len);

extern void write_out_of_bounds_error(pointer_info *p,
                                      size_t dst_sz,
                                      size_t src_sz);

extern void c_library_error(const char *function);

extern void load_store_error(pointer_info *p);


extern int gprintf(const options_t &Options,
                   output_parameter &P,
                   call_info &C,
                   pointer_info &FormatString,
                   va_list Args);

extern int internal_printf(const options_t &options,
                           output_parameter &P,
                           call_info &C,
                           const char *fmt,
                           va_list args);

namespace
{
  using std::cerr;
  using std::endl;

  using namespace NAMESPACE_SC;

  //
  // Get the object boundaries of the pointer associated with the pointer_info
  // structure.
  //
  static inline void
  find_object(pointer_info *p)
  {
    pointer_info &P = *p;
    if (P.flags & ISRETRIEVED)
      return;

    DebugPoolTy *pool = (DebugPoolTy *) P.pool;
    if ((pool && pool->Objects.find(P.ptr, P.bounds[0], P.bounds[1])) ||
        ExternalObjects.find(P.ptr, P.bounds[0], P.bounds[1]))
    {
      P.flags |= HAVEBOUNDS;
    }
    else if (P.flags & ISCOMPLETE)
    {
      cerr << "Object not found in pool!" << endl;
      load_store_error(p);
    }
    P.flags |= ISRETRIEVED;
  }

  //
  // This function is identical to strnlen(), which is not found on Mac OS X.
  //
  static inline size_t
  _strnlen(const char *s, size_t max)
  {
    size_t i;
    for (i = 0; i < max; ++i)
      if (s[i] == '\0')
        break;
    return i;
  }
}

#endif
