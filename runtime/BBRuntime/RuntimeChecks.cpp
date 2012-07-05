//=====- RuntimeChecks.cpp - Implementation of poolallocator runtime -======//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements runtime checks used by SAFECode for BaggyBounds.
//
//===----------------------------------------------------------------------===//
// NOTES:
//  1) Some of the bounds checking code may appear strange.  The reason is that
//     it is manually inlined to squeeze out some more performance.  Please
//     don't change it.
//
//===----------------------------------------------------------------------===//

#include "ConfigData.h"
#include "DebugReport.h"

#include "safecode/Runtime/BBRuntime.h"
#include "safecode/Runtime/BBMetaData.h"
#include "../include/CWE.h"

#include <map>
#include <cstdarg>
#include <stdint.h>

#define TAG unsigned tag

#include <stdio.h>

extern FILE * ReportLog;
extern unsigned char* __baggybounds_size_table_begin; 
extern unsigned SLOT_SIZE;
extern unsigned SLOTSIZE;
extern unsigned WORD_SIZE;
extern const unsigned int  logregs;
using namespace NAMESPACE_SC;

//
// Function: isOOB()
//  
// Description:
//  This function determines whether p is an OOB pointer or not. In our BBC  
//  implementation, if p is in kernel address space, it is an OOB pointer and 
//  returns true. On x86_64 machine, kernel address space is greater than
//  0xffff800000000000 and on x86_32 machine with Linux OS, it is greater than
//  0xc0000000. Current we only handle 64-bit OS and 32-bit Linux OS.
//
static inline int isOOB(uintptr_t p) {
  return (p & SET_MASK);
}

//
// Function: isInUpperHalf()
//
// Description:
//  This function determines whether p that is within SLOTSIZE/2 bytes from the
//  the original object is pointing to an address before the beginning of the memory 
//  object or after the end of the memory object. Since in BBC, the allocation bounds 
//  are aligned to slot boundaries we can find if a OOB pointer is below or above the
//  allocation by checking whether it lies in the top or the bottom half of a memory 
//  slot respectively. If p underflowed the buffer,then it will be in the second half
//  of the slot that precedes the referent. If p overflowed the memory object, then p 
//  will point in the first half of the slot that comes after the referent. This 
//  technique only handles OOB pointers within SLOTSIZE/2 bytes from the original object.
//  More details can see Section 2.4 of BBC paper (Baggy Bounds Checking: An Efficient
//  and Backwards-Compatiable Defense against Out-of-Bounds Errors)
//
static inline int isInUpperHalf(uintptr_t p) {
  return (p & SLOTSIZE/2);
}

//
// Function: getActualValue()
//
// Description:
//  This function returns the actual value of a marked OOB pointer.
//
static inline uintptr_t getActualValue(uintptr_t p) {
  return (p & UNSET_MASK);
}

//
// Function: rewritePtr()
//
// Description:
//  This function mark an OOB pointer to be the value that is in the kernel
//  address space by seeting some significant bits.
//
static inline uintptr_t rewritePtr(uintptr_t p) {
  return (p | SET_MASK);
}

static inline int
_barebone_pointers_in_bounds(uintptr_t Source, uintptr_t Dest) {
  unsigned char e;
  e = __baggybounds_size_table_begin[Source >> SLOT_SIZE];

  if (e == 0)
    return Source != Dest;

  uintptr_t begin = Source & ~((1<<e)-1);
  BBMetaData *data = (BBMetaData*)(begin + (1<<e) - sizeof(BBMetaData));
  uintptr_t end = begin + data->size;

  return !(begin <= Source && Source < end && begin <= Dest && Dest < end);
}

//
// Function: _barebone_boundscheck()
//
// Description:
//  Perform an accurate bounds check for the given pointer.  This function
//  encapsulates the logic necessary to do the check.
//
// Return value:
//  The dest pointer if it is in bounds, else an OOB pointer.
//  
//
static inline void*
_barebone_boundscheck (uintptr_t Source, uintptr_t Dest) {
  //
  // Check if it is an OOB pointer
  //
  uintptr_t val = 1 ;
  unsigned char e;

  e = __baggybounds_size_table_begin[Source >> SLOT_SIZE];
  val = _barebone_pointers_in_bounds(Source, Dest);

  if (val) {
    if (isOOB(Source)) {
      //
      // This means that Source is an OOB pointer
      //
      Source = getActualValue(Source);
      Source += ((isInUpperHalf(Source)) ? SLOTSIZE : -SLOTSIZE);
      Dest = getActualValue(Dest);
   } 
  //
  // Look for the bounds in the table
  //
    e = __baggybounds_size_table_begin[Source >> SLOT_SIZE];
    if (e == 0) {
      return (void*)Dest;
    }
    val = _barebone_pointers_in_bounds(Source, Dest);

  //
  //Set high bit, for OOB pointer 
  //

    if (val) {
        Dest = rewritePtr(Dest);
    }
  }
  return (void*)Dest;
}

//
// Function: poolcheck_debug()
//
// Description:
//  This function performs a load/store check.  It ensures that the given
//  pointer points into a valid memory object.
//
void
bb_poolcheck_debug (DebugPoolTy *Pool,
                 void *Node,
                 TAG,
                 const char * SourceFilep,
                 unsigned lineno) {
  //
  // Check if is an OOB pointer
  //
  if ((uintptr_t)Node & SET_MASK) {
    DebugViolationInfo v;
    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = Node,
    v.CWE = CWEBufferOverflow,
    v.SourceFile = SourceFilep,
    v.lineNo = lineno;

    ReportMemoryViolation(&v);
    return;
  }
  return;
}

void
bb_poolcheckui_debug (DebugPoolTy *Pool,
                 void *Node,
                 TAG,
                 const char * SourceFilep,
                 unsigned lineno) {
  //
  // Check if is an OOB pointer
  //
  if ((uintptr_t)Node & SET_MASK) {
    DebugViolationInfo v;
    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = Node,
    v.CWE = CWEBufferOverflow,
    v.SourceFile = SourceFilep,
    v.lineNo = lineno;

    ReportMemoryViolation(&v);
    return;
  }
  return;
}

extern "C" void
poolcheckui_debug (DebugPoolTy *Pool,
                 void *Node,
                 unsigned length, TAG,
                 const char * SourceFilep,
                 unsigned lineno) {
  bb_poolcheckui_debug(Pool, Node, tag, SourceFilep, lineno);
}

//
// Function: poolcheckalign_debug()
//
// Description:
//  Identical to poolcheckalign() but with additional debug info parameters.
//
// Inputs:
//  Pool   - The pool in which the pointer should be found.
//  Node   - The pointer to check.
//  Offset - The offset, in bytes, that the pointer should be to the beginning
//           of objects found in the pool.
//
void
bb_poolcheckalign_debug (DebugPoolTy *Pool, 
                         void *Node, 
                         unsigned Offset, TAG, 
                         const char * SourceFile, 
                         unsigned lineno) {
  //
  // Check if is an OOB pointer
  //
  if ((uintptr_t)Node & SET_MASK) {

    //
    // The object has not been found.  Provide an error.
    //
    DebugViolationInfo v;
    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = Node,
    v.CWE = CWEBufferOverflow,
    v.SourceFile = SourceFile,
    v.lineNo = lineno;

    ReportMemoryViolation(&v);
  }
  return;
}

void
bb_poolcheckui (DebugPoolTy *Pool, void *Node) {
  return bb_poolcheckui_debug(Pool, Node, 0, NULL, 0);
}



//
// Function: boundscheck_debug()
//
// Description:
//  Identical to boundscheck() except that it takes additional debug info
//  parameters.
//
// FIXME: this function is marked as noinline due to LLVM bug 4562
// http://llvm.org/bugs/show_bug.cgi?id=4562
//
// the attribute should be taken once the bug is fixed.
void * __attribute__((noinline))
bb_boundscheck_debug (DebugPoolTy * Pool, 
                      void * Source, 
                      void * Dest, TAG, 
                      const char * SourceFile, 
                      unsigned lineno) {
  return _barebone_boundscheck((uintptr_t)Source, (uintptr_t)Dest);
}

//
// Function: boundscheckui_debug()
//
// Description:
//  Identical to boundscheckui() but with debug information.
//
// Inputs:
//  Pool       - The pool to which the pointers (Source and Dest) should
//               belong.
//  Source     - The Source pointer of the indexing operation (the GEP).
//  Dest       - The result of the indexing operation (the GEP).
//  SourceFile - The source file in which the check was inserted.
//  lineno     - The line number of the instruction for which the check was
//               inserted.
//
void *
bb_boundscheckui_debug (DebugPoolTy * Pool,
                     void * Source,
                     void * Dest, TAG,
                     const char * SourceFile,
                     unsigned int lineno) {
  return  _barebone_boundscheck((uintptr_t)Source, (uintptr_t)Dest);
}

extern "C" void *
boundscheckui_debug (DebugPoolTy * Pool,
                     void * Source,
                     void * Dest, TAG,
                     const char * SourceFile,
                     unsigned int lineno) {
  return bb_boundscheckui_debug(Pool, Source, Dest, tag, SourceFile, lineno);
}


/// Stubs

void
bb_poolcheck (DebugPoolTy *Pool, void *Node) {
  bb_poolcheck_debug(Pool, Node, 0, NULL, 0);
}

//
// Function: boundscheck()
//
// Description:
//  Perform a precise bounds check.  Ensure that Source is within a valid
//  object within the pool and that Dest is within the bounds of the same
//  object.
//
void *
bb_boundscheck (DebugPoolTy * Pool, void * Source, void * Dest) {
  return bb_boundscheck_debug(Pool, Source, Dest, 0, NULL, 0);
}

//
// Function: boundscheckui()
//
// Description:
//  Perform a bounds check (with lookup) on the given pointers.
//
// Inputs:
//  Pool - The pool to which the pointers (Source and Dest) should belong.
//  Source - The Source pointer of the indexing operation (the GEP).
//  Dest   - The result of the indexing operation (the GEP).
//
void *
bb_boundscheckui (DebugPoolTy * Pool, void * Source, void * Dest) {
  return bb_boundscheckui_debug (Pool, Source, Dest, 0, NULL, 0);
}

//
// Function: poolcheckalign()
//
// Description:
//  Ensure that the given pointer is both within an object in the pool *and*
//  points to the correct offset within the pool.
//
// Inputs:
//  Pool   - The pool in which the pointer should be found.
//  Node   - The pointer to check.
//  Offset - The offset, in bytes, that the pointer should be to the beginning
//           of objects found in the pool.
//
void
bb_poolcheckalign (DebugPoolTy *Pool, void *Node, unsigned Offset) {
  bb_poolcheckalign_debug(Pool, Node, Offset, 0, NULL, 0);
}

void *
pchk_getActualValue (DebugPoolTy * Pool, void * ptr) {
  uintptr_t Source = (uintptr_t)ptr;
  if (isOOB(Source)) {
    Source = getActualValue(Source);
  }
  
  return (void*)Source;
}

//
// Function: funccheck()
//
// Description:
//  Determine whether the specified function pointer is one of the functions
//  in the given list.
//
// Inputs:
//  num - The number of function targets in the DSNode.
//  f   - The function pointer that we are testing.
//  g   - The first function given in the DSNode.
//
void
__sc_bb_funccheck (unsigned num, void *f, void *g, ...) {
  va_list ap;
  unsigned i = 0;

  // Test against the first function in the list
  if (f == g) return;
  i++;
  va_start(ap, g);
  for ( ; i != num; ++i) {
    void *h = va_arg(ap, void *);
    if (f == h) {
      return;
    }
  }
  abort();
}

//
// Function: fastlscheck()
//
// Description:
//  This function performs a fast load/store check.  If the check fails, it
//  will *not* attempt to do pointer rewriting.
//
// Inputs:
//  base   - The address of the first byte of a memory object.
//  result - The pointer that is being checked.
//  size   - The size of the object in bytes.
//  lslen  - The length of the data accessed in memory.
//

extern "C" void
fastlscheck_debug(const char *base, const char *result, unsigned size,
                   unsigned lslen,
                   unsigned tag,
                   const char * SourceFile,
                   unsigned lineno) {
  //
  // If the pointer is within the object, the check passes.  Return the checked
  // pointer.
  //
  const char * end = result + lslen - 1;
  if ((result >= base) && (result < (base + size))) {
    if ((end >= base) && (end < (base + size))) {
      return;
    }
  }

  //
  // Check failed. Provide an error.
  //
  DebugViolationInfo v;
  v.type = ViolationInfo::FAULT_LOAD_STORE,
  v.faultPC = __builtin_return_address(0),
  v.faultPtr = result,
  v.CWE = CWEBufferOverflow,
  v.dbgMetaData = NULL,
  v.SourceFile = SourceFile,
  v.lineNo = lineno;
  
  ReportMemoryViolation(&v);
  
  return;
}

//
// Function: poolcheck_freeui_debug()
//
// Description:
//  Check that freeing the pointer is correct.  Permit incomplete and unknown
//  pointers.
//
void
bb_poolcheck_freeui_debug (DebugPoolTy *Pool,
                      void * ptr,
                      unsigned tag,
                      const char * SourceFilep,
                      unsigned lineno) {
  //
  // Ignore frees of NULL pointers.  These are okay.
  //
  if (ptr == NULL)
    return;

  //
  // Retrieve the bounds information for the object.  Use the pool that tracks
  // debug information since we're in debug mode.
  //
  unsigned char e;
  e = __baggybounds_size_table_begin[(uintptr_t)ptr >> SLOT_SIZE];

  uintptr_t ObjStart = (uintptr_t)ptr & ~((1<<e)-1);
  BBMetaData *data = (BBMetaData*)(ObjStart + (1<<e) - sizeof(BBMetaData));
  uintptr_t ObjLen = data->size;

  //
  // Determine if we're freeing a pointer that doesn't point to the beginning
  // of an object.  If so, report an error.
  //
  if ((uintptr_t)ptr != ObjStart) {
    OutOfBoundsViolation v;
    v.type = ViolationInfo::FAULT_INVALID_FREE,
      v.faultPC = __builtin_return_address(0),
      v.faultPtr = ptr,
      v.CWE = CWEFreeNotStart,
      v.SourceFile = SourceFilep,
      v.lineNo = lineno,
      v.objStart = (void *)ObjStart;
      v.objLen = ObjLen;
    ReportMemoryViolation(&v);
  }

  return;
}

extern "C" void
poolcheck_freeui_debug (DebugPoolTy *Pool,
                      void * ptr,
                      unsigned tag,
                      const char * SourceFilep,
                      unsigned lineno) {
  bb_poolcheck_freeui_debug(Pool, ptr, tag, SourceFilep, lineno);
}


//
//
// Function: poolcheck_free_debug()
//
// Description:
//  Check that freeing the pointer is correct.
//
void
bb_poolcheck_free_debug (DebugPoolTy *Pool,
                      void * ptr,
                      unsigned tag,
                      const char * SourceFilep,
                      unsigned lineno) {
  bb_poolcheck_freeui_debug(Pool, ptr, tag, SourceFilep, lineno);
}

//
// Function: poolcheck_free()
//
// Description:
//  Check that freeing the pointer is correct.
//
void
bb_poolcheck_free (DebugPoolTy *Pool, void * ptr) {
  bb_poolcheck_free_debug(Pool, ptr, 0, NULL, 0);
}

//
// Function: poolcheck_freeui()
//
// Description:
//  The incomplete version of poolcheck_free().
//
void
poolcheck_freeui (DebugPoolTy *Pool, void * ptr) {
  bb_poolcheck_freeui_debug(Pool, ptr, 0, NULL, 0);
}

