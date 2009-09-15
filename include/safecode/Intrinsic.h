//===- Intrinsic.cpp - Insert declaration of SAFECode intrinsics -*- C++ -*---//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a module pass to insert declarations of the SAFECode
// intrinsics into the bitcode file. It also provides interfaces for later
// passes which use these intrinsics.
// FIXME:
// This pass is a utility pass now since it contains various good stuffs
// here. Rename required.
//
//===----------------------------------------------------------------------===//

#ifndef _SC_INTRINSIC_H_
#define _SC_INTRINSIC_H_

#include "safecode/SAFECode.h"

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/StringMap.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

class InsertSCIntrinsic : public ModulePass {
  public:
    static char ID;
    enum IntrinsicFlags {
      SC_INTRINSIC_NO_OP	    	= 0,      // No-op intrinsic
      SC_INTRINSIC_HAS_POOL_HANDLE      = 1,
      SC_INTRINSIC_HAS_VALUE_POINTER    = 1 << 1,
      SC_INTRINSIC_CHECK         	= 1 << 2,
      // Memory check intrinsic
      SC_INTRINSIC_MEMCHECK     	= 1 << 3,
      // Indexing (GEP) check intrinsic
      SC_INTRINSIC_BOUNDSCHECK     	= 1 << 4,
      // Out-of-bounds pointer rewriting
      SC_INTRINSIC_REGISTRATION        	= 1 << 5,
      SC_INTRINSIC_OOB                  = 1 << 6,
      SC_INTRINSIC_POOL_CONTROL         = 1 << 7,
      SC_INTRINSIC_DEBUG_INSTRUMENTATION= 1 << 8,
      SC_INTRINSIC_MISC                 = 1 << 9
    } IntrinsicFlags;

    typedef struct IntrinsicInfo {
      // The type of intrinsic check
      unsigned int flag;

      // The function for the intrinsic
      Function * F;

      // For checking intrinsics, the operand index of the pointer to check
      unsigned ptrindex;
    } IntrinsicInfoTy;

    InsertSCIntrinsic() : ModulePass((intptr_t)&ID), currentModule(NULL) {}
    virtual ~InsertSCIntrinsic() {}
    virtual bool runOnModule(Module & M);
    virtual const char * getPassName() const {
      return "Insert declaration of SAFECode Intrinsic";
    }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.setPreservesCFG();
      AU.setPreservesAll();
    }

    void addIntrinsic (const char * name,
                       unsigned int flag,
                       FunctionType * FTy,
                       unsigned index = 0);
    const IntrinsicInfoTy & getIntrinsic(const std::string & name) const;
    bool isSCIntrinsicWithFlags(Value * inst, unsigned flag) const;
    Value * getValuePointer (CallInst * CI);
    typedef std::vector<IntrinsicInfoTy> IntrinsicInfoListTy;
    typedef IntrinsicInfoListTy::const_iterator intrinsic_const_iterator;
    intrinsic_const_iterator intrinsic_begin() const { return intrinsics.begin(); }
    intrinsic_const_iterator intrinsic_end() const { return intrinsics.end(); }
    Value * getObjectSize(Value * V);
    Value * findObject (Value * V);

  private:
    TargetData * TD;
    Module * currentModule;
    IntrinsicInfoListTy intrinsics;
    StringMap<uint32_t> intrinsicNameMap;
    void addDebugIntrinsic(const char * name);

};

NAMESPACE_SC_END

#endif
