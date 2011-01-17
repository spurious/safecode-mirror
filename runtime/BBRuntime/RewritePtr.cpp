//===- RewritePtr.cpp --------------------------------------------------- -===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements functions to rewrite out of bounds pointers.
//
//===----------------------------------------------------------------------===//

#include "DebugReport.h"
#include "RewritePtr.h"

#include "llvm/ADT/DenseMap.h"

#include "safecode/Runtime/BBRuntime.h"

#include <map>
extern FILE * ReportLog;
using namespace NAMESPACE_SC; 

//
// Function: getActualValue()
//
// Description:
//  If src is an out of object pointer, get the original value.
//
/*void *
baggy_pchk_getActualValue (DebugPoolTy * Pool, void * p) {
  //
  // If the pointer is not within the rewrite pointer range, then it is not a
  // rewritten pointer.  Simply return its current value.
  //
  if(!((uintptr_t)p & 0x80000000))  return p;
  if (((uintptr_t)p <= InvalidLower) || ((uintptr_t)p >= InvalidUpper)) {
    return p;
  }
    return p;

  //
  // Look for the pointer in the pool's OOB pointer list.  If we find it,
  // return its actual value.
  //
}*/

