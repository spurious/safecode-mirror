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

NAMESPACE_SC_BEGIN

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

  // Source filename
  void * SourceFile;

  // Line number
  unsigned lineno;
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

NAMESPACE_SC_END

extern "C" {
  void pool_init_runtime(unsigned Dangling,
                         unsigned RewriteOOB,
                         unsigned Terminate);
  void * __sc_dbg_newpool(unsigned NodeSize);
  void __sc_dbg_pooldestroy(NAMESPACE_SC::DebugPoolTy * Pool);
  void * __sc_dbg_poolalloc(NAMESPACE_SC::DebugPoolTy * Pool, unsigned NumBytes);

  void __sc_dbg_poolargvregister (int argc, char ** argv);
  void __sc_dbg_poolregister(NAMESPACE_SC::DebugPoolTy * Pool, void *allocaptr, unsigned NumBytes);
  void __sc_dbg_poolunregister(NAMESPACE_SC::DebugPoolTy * Pool, void *allocaptr);
  void __sc_dbg_poolfree(NAMESPACE_SC::DebugPoolTy * Pool, void *Node);

  void poolcheck(NAMESPACE_SC::DebugPoolTy * Pool, void *Node);
  void poolcheckui(NAMESPACE_SC::DebugPoolTy * Pool, void *Node);
  void * boundscheck   (NAMESPACE_SC::DebugPoolTy * Pool, void * Source, void * Dest);
  void * boundscheckui (NAMESPACE_SC::DebugPoolTy * Pool, void * Source, void * Dest);
  void * boundscheckui_debug (NAMESPACE_SC::DebugPoolTy * P, void * S, void * D,
  const char * SFile, unsigned int lineno);
  void funccheck (unsigned num, void *f, void *g, ...);
  void poolcheckalign(NAMESPACE_SC::DebugPoolTy * Pool, void *Node, unsigned Offset);

  void * __sc_dbg_src_poolcalloc (NAMESPACE_SC::DebugPoolTy * Pool,
                                unsigned Number, unsigned NumBytes,
                                const char * SourceFilep,
                                unsigned lineno);
  void * __sc_dbg_poolcalloc (NAMESPACE_SC::DebugPoolTy *Pool, unsigned Number, unsigned NumBytes);

  void * __sc_dbg_src_poolalloc (NAMESPACE_SC::DebugPoolTy * P, unsigned Size, const char * SrcFle, unsigned no);
  void * __sc_dbg_poolrealloc(NAMESPACE_SC::DebugPoolTy *Pool, void *Node, unsigned NumBytes);
  void __sc_dbg_src_poolregister (NAMESPACE_SC::DebugPoolTy * P, void * p,
  unsigned size, const char * SF, unsigned lineno);
  void   __sc_dbg_src_poolfree (NAMESPACE_SC::DebugPoolTy * P, void * ptr, const char * SrcFle, unsigned no);
  void   poolcheck_debug (NAMESPACE_SC::DebugPoolTy * P, void * Node, const char * SrcFle, unsigned no);
  void   poolcheckalign_debug (NAMESPACE_SC::DebugPoolTy * P, void *Node, unsigned Offset, void * SourceFile, unsigned lineno);
  void * boundscheck_debug (NAMESPACE_SC::DebugPoolTy * P, void * S, void * D,
  const char * SFile, unsigned int lineno);
  void * pchk_getActualValue (NAMESPACE_SC::DebugPoolTy * Pool, void * src);

  // Exact checks
  void * exactcheck2 (const char *base, const char *result, unsigned size);
  void * exactcheck2_debug (const char *base, const char *result, unsigned size,
  const char *, unsigned);

}
#endif
