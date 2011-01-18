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
#include "RewritePtr.h"

#include "safecode/Runtime/BBRuntime.h"

#include <map>
#include <cstdarg>
#include <cassert>

#define TAG unsigned tag

#if defined(__linux__)
static const uintptr_t InvalidUpper = 0xffff700000000000;
static const uintptr_t InvalidLower = 0xffff800000000000;
#else
extern uintptr_t InvalidUpper;
extern uintptr_t InvalidLower;
#endif


extern FILE * ReportLog;
extern unsigned char* __baggybounds_size_table_begin; 
extern unsigned SLOT_SIZE;
extern unsigned WORD_SIZE;
extern const unsigned int  logregs;
using namespace NAMESPACE_SC;

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
  uintptr_t val =1 ;
  unsigned char e;

  e = __baggybounds_size_table_begin[Source >> SLOT_SIZE];
  val = (Source^Dest)>>e;
  if(val) {
  
    if(Source & 0xffff800000000000) {

      Source = Source & 0x7fffffffffff;
      if(Source & 0x8) {
        Source +=16;
      } else {
        Source -=16;
      }
      Dest = Dest & 0x7fffffffffff;
   } 
  //
  // Look for the bounds in the table
  //
    e = __baggybounds_size_table_begin[Source >> SLOT_SIZE];
    if(e == 0) {
      return (void*)Dest;
    }
    val = (Source^Dest)>>e;

  //
  //Set high bit, for OOB pointer 
  //

    if(val) {
  //    return rewrite_ptr(NULL, (void*)Source, (void*)Source, (void*)Source, "<unknown", 0); 
        Dest = (Dest | 0xffff800000000000);
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
baggy_poolcheck_debug (DebugPoolTy *Pool,
                 void *Node,
                 TAG,
                 const char * SourceFilep,
                 unsigned lineno) {
  //
  // Check if is an OOB pointer
  //
    if((uintptr_t)Node& 0xffff800000000000) {
  
    DebugViolationInfo v;
    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = Node,
    v.SourceFile = SourceFilep,
    v.lineNo = lineno;

    ReportMemoryViolation(&v);
    assert(false);
    return;
  }
  return;

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
// FIXME:
//  For now, this does nothing, but it should, in fact, do a run-time check.
//
void
bb_poolcheckalign_debug (DebugPoolTy *Pool, void *Node, unsigned Offset, TAG, const char * SourceFile, unsigned lineno) {
  //
  // Check if is an OOB pointer
  //
  //if (isRewritePtr (Node)) {// If it is OOB ptr, report a violation 
    if((uintptr_t)Node& 0xffff800000000000) {

  //
  // The object has not been found.  Provide an error.
  //
    /*if (logregs) {
      fprintf (stderr, "PoolCheck : Violation(A):  %p %d \n",  Node, Offset );
      fflush (stderr);
    }*/
    DebugViolationInfo v;
    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = Node,
    v.SourceFile = SourceFile,
    v.lineNo = lineno;

    ReportMemoryViolation(&v);
    assert(false);
  }
  return;
}

void
bb_poolcheckui (DebugPoolTy *Pool, void *Node, TAG) {
    if((uintptr_t)Node& 0xffff800000000000) {
    assert(false);
  }
  return;
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
bb_boundscheck_debug (DebugPoolTy * Pool, void * Source, void * Dest, TAG, const char * SourceFile, unsigned lineno) {

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
bb_pchk_getActualValue (DebugPoolTy * Pool, void * ptr) {
  uintptr_t Source = (uintptr_t)ptr;
    if(Source & 0xffff800000000000) {
      Source = Source & 0x7fffffffffff;
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


