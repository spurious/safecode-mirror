//===- SAFECodeRuntime.h -- Runtime interface of SAFECode ------*- C++ -*-===//
// 
//                     The LLVM Compiler Infrast`ructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines the interface of SAFECode runtime library.
//
//===----------------------------------------------------------------------===//

#ifndef _SAFECODE_RUNTIME_H_
#define _SAFECODE_RUNTIME_H_

#include "safecode/SAFECode.h"
#include "safecode/Runtime/BitmapAllocator.h"
#include "poolalloc_runtime/Support/SplayTree.h"

#include <iosfwd>

NAMESPACE_SC_BEGIN

//
// Enumerated Type: allocType
//
// Description:
//  This enumerated type lists the different types of allocations that can be
//  made.
//
enum allocType {
  Global,   // Global object
  Stack,    // Stack-allocated object
  Heap      // Heap-allocated object
};

//
// Structure: DebugMetaData
//
// Description:
//  This structure contains information on the error to be reported.
//
// Fields:
//  allocID    : The ID number of the allocation of the object.
//  freeID     : The ID number of the deallocation of the object.
//  allocPC    : The program counter at which the object was last allocated.
//  freePC     : The program counter at which the object was last deallocated.
//  canonAddr  : The canonical address of the memory reference.
//  SourceFile : A string containing the source file to which the erring
//               instruction is found.
//  lineno     : The line number in the source file to which the erring
//               instruction is found.
//
typedef struct DebugMetaData {
  unsigned allocID;
  unsigned freeID;
  void * allocPC;
  void * freePC;
  void * canonAddr;

  // Allocation type (global, stack, or heap object)
  allocType allocationType;

  // Source filename
  void * SourceFile;

  // Source filename for deallocation
  void * FreeSourceFile;

  // Line number
  unsigned lineno;

  // Line number for deallocation
  unsigned Freelineno;

  void print(std::ostream & OS) const;
} DebugMetaData;
typedef DebugMetaData * PDebugMetaData;

struct DebugPoolTy : public BitmapPoolTy {
  // Splay tree used for object registration
  RangeSplaySet<> Objects;
  // Splay tree used for out of bound objects
  RangeSplayMap<void *> OOB;
  // Splay tree used by dangling pointer runtime
  RangeSplayMap<PDebugMetaData> DPTree;
};

void * rewrite_ptr (DebugPoolTy * Pool, const void * p, const void * ObjStart,
const void * ObjEnd, const char * SourceFile, unsigned lineno);
void installAllocHooks (void);

NAMESPACE_SC_END

// Use macros so that I won't polluate the namespace

#define PPOOL NAMESPACE_SC::DebugPoolTy*
#define TAG unsigned
#define SRC_INFO const char *, unsigned int

extern "C" {
  void pool_init_runtime(unsigned Dangling,
                         unsigned RewriteOOB,
                         unsigned Terminate);
  void * __sc_dbg_newpool(unsigned NodeSize);
  void __sc_dbg_pooldestroy(PPOOL);

  void * __sc_dbg_poolinit(PPOOL, unsigned NodeSize, unsigned);
  void * __sc_dbg_poolalloc(PPOOL, unsigned NumBytes);
  void * __sc_dbg_src_poolalloc (PPOOL, unsigned Size, TAG, SRC_INFO);

  void __sc_dbg_poolargvregister (int argc, char ** argv);

  void __sc_dbg_poolregister(PPOOL, void *allocaptr, unsigned NumBytes);
  void __sc_dbg_src_poolregister (PPOOL, void * p, unsigned size, TAG, SRC_INFO);
  void __sc_dbg_poolregister_stack (PPOOL, void * p, unsigned size);
  void __sc_dbg_src_poolregister_stack (PPOOL, void * p, unsigned size, TAG, SRC_INFO);
  void __sc_dbg_poolregister_global (PPOOL, void * p, unsigned size);
  void __sc_dbg_src_poolregister_global_debug (PPOOL, void * p, unsigned size, TAG, SRC_INFO);

  void __sc_dbg_poolunregister(PPOOL, void *allocaptr);
  void __sc_dbg_poolunregister_stack(PPOOL, void *allocaptr);
  void __sc_dbg_poolunregister_debug(PPOOL, void *allocaptr, TAG, SRC_INFO);
  void __sc_dbg_poolunregister_stack_debug(PPOOL, void *allocaptr, TAG, SRC_INFO);
  void __sc_dbg_poolfree(PPOOL, void *Node);
  void __sc_dbg_src_poolfree (PPOOL, void *, TAG, SRC_INFO);

  void * __sc_dbg_poolcalloc (PPOOL, unsigned Number, unsigned NumBytes, TAG);
  void * __sc_dbg_src_poolcalloc (PPOOL,
                                unsigned Number, unsigned NumBytes,
                                  TAG, SRC_INFO);

  void * __sc_dbg_poolrealloc(PPOOL, void *Node, unsigned NumBytes);
  void * __sc_dbg_poolstrdup (PPOOL, const char * Node);
  void * __sc_dbg_poolstrdup_debug (PPOOL, const char * Node, TAG, SRC_INFO);

  void poolcheck(PPOOL, void *Node);
  void poolcheckui(PPOOL, void *Node, TAG);
  void poolcheck_debug (PPOOL, void * Node, TAG, SRC_INFO);

  void poolcheckalign(PPOOL, void *Node, unsigned Offset);
  void poolcheckalign_debug (PPOOL, void *Node, unsigned Offset, TAG, SRC_INFO);

  void * boundscheck   (PPOOL, void * Source, void * Dest);
  void * boundscheckui (PPOOL, void * Source, void * Dest);
  void * boundscheckui_debug (PPOOL, void * S, void * D, TAG, SRC_INFO);
  void * boundscheck_debug (PPOOL, void * S, void * D, TAG, SRC_INFO);

  // CStdLib
  void * pool_memcpy(PPOOL dstPool, PPOOL srcPool, void *dst, const void *src, size_t n);
  void * pool_memmove(PPOOL dstPool, PPOOL srcPool, void *dst, const void *src, size_t n);
  void * pool_memset(PPOOL sPool, void *s, int c, size_t n);
  char * pool_strcpy(PPOOL dstPool, PPOOL srcPool, char *dst, const char *src);
  char * pool_strcpy_debug(PPOOL dstPool, PPOOL srcPool, char *dst, const char *src, TAG, SRC_INFO);
  size_t pool_strlen(PPOOL stringPool, const char *string);
  char * pool_strncpy(PPOOL dstPool, PPOOL srcPool, char *dst, const char *src, size_t n);

#ifdef _GNU_SOURCE
  void * pool_mempcpy(PPOOL dstPool, PPOOL srcPool, void *dst, const void *src, size_t n);
#endif

  // Exact checks
  void * exactcheck2 (const char *base, const char *result, unsigned size);
  void * exactcheck2_debug (const char *base, const char *result, unsigned size,
                            TAG, SRC_INFO);

  void __sc_dbg_funccheck (unsigned num, void *f, void *g, ...);
  void * pchk_getActualValue (PPOOL, void * src);

  // Change memory protections to detect dangling pointers
  void * pool_shadow (void * Node, unsigned NumBytes);
  void * pool_unshadow (void * Node);
}

#undef PPOOL
#undef TAG
#undef SRC_INFO
#endif
