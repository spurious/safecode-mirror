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
// Enable support for floating point numbers.
//
#define FLOATING_POINT

//
// The pointer_info structure and associated flags
// This holds a pointer argument to a format string function.
// This structure is initialized by a call to sc.fsparameter.
//
#define ISCOMPLETE  0x01 // Whether the pointer is complete according to DSA
#define ISRETRIEVED 0x02 // Whether there has been an attempt made to retrive
                         // the target object's boundaries
#define HAVEBOUNDS  0x04 // Whether the boundaries were retrieved successfully
#define NULL_PTR    0x08 // Whether the pointer in the structure is NULL

typedef struct
{
  void *ptr;             // The pointer which is wrapped by this structure
  void *pool;            // The pool to which the pointer belongs
  void *bounds[2];       // Space for retrieving object boundaries
  uint8_t flags;         // See above
} pointer_info;

//
// The call_info structure, which is initialized by sc.fscallinfo before a call
// to a format string function.
//
typedef struct
{
  uint32_t vargc;        // The number of varargs to this function call
  uint32_t tag;          // tag, line_no, source_file hold debug information
  uint32_t line_no;
  const char *source_info;
  void *whitelist[1];    // This is a list of pointer arguments that the
                         // format string function should treat as varargs
                         // arguments which are pointers. These arguments are
                         // all pointer_info structures. The list is terminated
                         // by a NULL element.
} call_info;

//
// This structure describes where to print the output for the internal printf()
// wrapper.
//
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
      pointer_info *info;
      char   *string;
      size_t pos;
      size_t maxsz;  // Maximum size of the array that can be written into the
                     // object safely. (SAFECode-imposed)
      size_t n;      // The maximum number of bytes to write. (user-imposed)
    } String;
    struct
    {
      char *string;
      size_t bufsz;
      size_t pos;
    } AllocedString;
  } Output;
} output_parameter;

#define USE_M_DIRECTIVE 0x0001 // Enable parsing of the %m directive for
                               // syslog()
typedef unsigned options_t;

//
// This structure describes where to get input characters for the internal
// scanf() wrapper.
//
typedef struct
{
  enum
  {
    INPUT_FROM_STREAM,
    INPUT_FROM_STRING
  } InputKind;
  union
  {
    struct
    {
      FILE *stream;
      char lastch;
    } Stream;
    struct
    {
      const char *string;
      size_t pos;
    } String;
  } Input;
} input_parameter;

//
// Error reporting functions
//
extern void out_of_bounds_error(call_info *c,
                                pointer_info *p,
                                size_t obj_len);

extern void write_out_of_bounds_error(call_info *c,
                                      pointer_info *p,
                                      size_t dst_sz,
                                      size_t src_sz);

extern void c_library_error(call_info *c, const char *function);

extern void load_store_error(call_info *c, pointer_info *p);

//
// Printing/scanning functions
//
extern int gprintf(const options_t &Options,
                   output_parameter &P,
                   call_info &C,
                   pointer_info &FormatString,
                   va_list Args);

extern int gscanf(input_parameter &P,
                  call_info &C,
                  pointer_info &FormatString,
                  va_list Args);

extern int internal_printf(const options_t &options,
                           output_parameter &P,
                           call_info &C,
                           const char *fmt,
                           va_list args);

extern int internal_scanf(input_parameter &p,
                          call_info &c,
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
  find_object(call_info *c, pointer_info *p)
  {
    pointer_info &P = *p;
    if (P.flags & ISRETRIEVED)
      return;

    DebugPoolTy *pool = (DebugPoolTy *) P.pool;
    if (P.ptr == 0)
      P.flags |= NULL_PTR;
    else if ((pool && pool->Objects.find(P.ptr, P.bounds[0], P.bounds[1])) ||
        ExternalObjects.find(P.ptr, P.bounds[0], P.bounds[1]))
    {
      P.flags |= HAVEBOUNDS;
    }
    else if (P.flags & ISCOMPLETE)
    {
      cerr << "Object not found in pool!" << endl;
      load_store_error(c, p);
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
