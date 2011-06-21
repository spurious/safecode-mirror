//===- FormatStrings.cpp - Format string function implementation ----------===//
// 
//                            The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements intrinsics and runtime wrapper functions for format
// string functions.
//
//===----------------------------------------------------------------------===//

#include "FormatStrings.h"
#include "PoolAllocator.h"
#include "DebugReport.h"

#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>

#include <iostream>
using std::cerr;
using std::endl;

//
// To do
//
// - Add support for detecting overlapping writes.
// - Add support for puts() which is transformed by compilers from simple
//   printf() statements.
//

using namespace NAMESPACE_SC;

//
// Functions to report errors.
//
void out_of_bounds_error(pointer_info *p, size_t obj_len)
{
  OutOfBoundsViolation v;
  v.type        = ViolationInfo::FAULT_OUT_OF_BOUNDS;
  v.faultPC     = __builtin_return_address(0);
  v.faultPtr    = p->ptr;
  v.SourceFile  = "";
  v.lineNo      = 0;
  v.PoolHandle  = p->pool;
  v.objLen      = obj_len;
  v.dbgMetaData = NULL;
  ReportMemoryViolation(&v);
}

void write_out_of_bounds_error(pointer_info *p, size_t dst_sz, size_t src_sz)
{
  WriteOOBViolation v;
  v.type        = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS;
  v.faultPC     = __builtin_return_address(0);
  v.faultPtr    = p->ptr;
  v.SourceFile  = "";
  v.lineNo      = 0;
  v.PoolHandle  = p->pool;
  v.dstSize     = dst_sz;
  v.srcSize     = src_sz;
  v.dbgMetaData = NULL;
  ReportMemoryViolation(&v);
}

void c_library_error(const char *function)
{
  CStdLibViolation v;
  v.type        = ViolationInfo::FAULT_CSTDLIB;
  v.faultPC     = __builtin_return_address(0);
  v.faultPtr    = 0;
  v.SourceFile  = "";
  v.lineNo      = 0;
  v.function    = function;
  v.dbgMetaData = NULL;
  ReportMemoryViolation(&v);
}

void load_store_error(pointer_info *p)
{
  DebugViolationInfo v;
  v.type        = ViolationInfo::FAULT_LOAD_STORE;
  v.faultPC     = __builtin_return_address(0);
  v.faultPtr    = p->ptr;
  v.dbgMetaData = NULL;
  v.SourceFile  = "";
  v.lineNo      = 0;
  v.PoolHandle  = NULL;
  ReportMemoryViolation(&v);
}

//
// Store the given pointer/pool/completeness information into a pointer_info
// structure that gets passed into the transformed format string function.
//
// Inputs:
//     _pool:    pointer to the pool to store
//       ptr:    pointer to store
//     _dest:    pointer to the pointer_info structure to write the information
//               info
//  complete:    completeness byte
//
// This function returns a pointer to the pointer_info structure (= _dest).
//
void *__sc_fsparameter(void *_pool, void *ptr, void *_dest, uint8_t complete)
{
  DebugPoolTy *pool = (DebugPoolTy *) _pool;
  pointer_info *dest = (pointer_info *) _dest;

  dest->ptr  = ptr;
  dest->pool = pool;
  dest->flags = complete;

  return dest;
}

//
// Register information about a call to a secured format string function.
// This information is stored into a call_info structure that gets passed into
// the secured format string function.
//
// Inputs:
//  _dest:  A pointer to the call_info structure to write the information into
//  vargc:  The number of varargs arguments to the call to the function
//
//  The NULL-ended variable argument list consists of the vararg parameters to
//  the format string function which are pointer_info structures. The secured
//  format string function will only access these values as pointers.
//
// This function returns a pointer to the call_info structure (= _dest).
//
void *__sc_fscallinfo(void *_dest, uint32_t vargc, ...)
{
  va_list ap;
  call_info *dest = (call_info *) _dest;

  dest->vargc = vargc;

  void *arg;
  unsigned argpos = 0;

  va_start(ap, vargc);
  do
  {
    arg = va_arg(ap, void *);
    dest->whitelist[argpos++] = arg;
  } while (arg != 0);
  va_end(ap);

  return dest;
}

//
// Secure runtime wrapper to replace printf()
//
int pool_printf(void *_info, void *_fmt, ...)
{
  va_list ap;
  int result;
  pointer_info *fmt  = (pointer_info *) _fmt;
  call_info    *call = (call_info *)    _info;
  options_t options = 0x0;

  //
  // Set up the output parameter structure to point to stdout as the output
  // file.
  //
  output_parameter P;
  P.OutputKind  = output_parameter::OUTPUT_TO_FILE;
  P.Output.File = stdout;

  //
  // Lock stdout before calling the function which does the printing.
  //
  flockfile(stdout);
  va_start(ap, _fmt);
  result = gprintf(options, P, *call, *fmt, ap);
  va_end(ap);
  funlockfile(stdout);

  return result;
}

//
// Secure general printf()-family replacement
//
// Attempts to verify the following:
//  - The output string is not written to out of bounds, if there is an output
//    string specified.
//  - The format string is not read out of bounds.
//  - A %s format directive will not result in an out of bounds read of a
//    string.
//  - A %n format directive will not result in an out of bounds write into
//    a parameter.
//
// Arguments:
//  Output:        Information about where to print the output
//  CInfo:         Information about the call parameters (call_info structure)
//  FormatString:  pointer_info structure holding the format string
//  Args:          The list of arguments to the format string
//
// Return values:
//  If successful, the function returns the number of characters that would have
//  been printed had the output been unbounded.
//
//  If a (non memory-safety) error occurred, the function returns a negative
//  value.
//
int
gprintf(const options_t &Options,
        output_parameter &Output,
        call_info &CInfo,
        pointer_info &FormatString,
        va_list Args)
{
  int result;
  const char *Fmt;
  //
  // Get the object boundaries for the format string.
  //
  find_object(&FormatString);
  Fmt = (const char *) FormatString.ptr;
  //
  // Make sure the format string isn't NULL.
  //
  if (Fmt == 0)
  {
    cerr << "NULL format string!" << endl;
    return 0;
  }
  //
  // Check to make sure the format string is within object boundaries, if we
  // have them.
  //
  if (FormatString.flags & HAVEBOUNDS)
  {
    size_t maxbytes = 1 + (char *) FormatString.bounds[1] - Fmt;
    size_t len = _strnlen(Fmt, maxbytes);
    if (len == maxbytes)
    {
      cerr << "Format string not terminated within object bounds!" << endl;
      out_of_bounds_error(&FormatString, len);
    }
  }

  result = internal_printf(Options, Output, CInfo, Fmt, Args);
  return result;
}
