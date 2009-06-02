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

#include <set>
#include <map>

using namespace llvm;

NAMESPACE_SC_BEGIN

class InsertSCIntrinsic : public ModulePass {
  public:
    static char ID;
    typedef enum IntrinsicType {
      SC_INTRINSIC_NO_OP,      // No-op intrinsic
      SC_INTRINSIC_MEMCHECK,   // Memory check intrinsic
      SC_INTRINSIC_GEPCHECK,   // Indexing (GEP) check intrinsic
      SC_INTRINSIC_OOB,
      SC_INTRINSIC_POOL_CONTROL,
      SC_INTRINSIC_MISC,
      SC_INTRINSIC_COUNT
    } IntrinsicType;

    typedef struct IntrinsicInfo {
      // The type of intrinsic check
      IntrinsicType type;

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
    void addIntrinsic (IntrinsicType type,
                       const std::string & name,
                       FunctionType * FTy,
                       unsigned index=0);
    const IntrinsicInfoTy & getIntrinsic(const std::string & name) const;
    bool isSCIntrinsic(Value * V) const;
    bool isCheckingIntrinsic(Value * V) const;
    bool isGEPCheckingIntrinsic (Value * V) const;
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.setPreservesCFG();
      AU.setPreservesAll();
    }
    Value * getCheckedPointer (CallInst * CI);
    typedef std::vector<IntrinsicInfoTy> IntrinsicInfoListTy;
    typedef IntrinsicInfoListTy::const_iterator intrinsic_const_iterator;
    intrinsic_const_iterator intrinsic_begin() const { return intrinsics.begin(); }
    intrinsic_const_iterator intrinsic_end() const { return intrinsics.end(); }
    Value * getObjectSize(Value * V);

  private:
    TargetData * TD;
    Module * currentModule;
    IntrinsicInfoListTy intrinsics;
    std::map<std::string, uint32_t> intrinsicNameMap;
};

NAMESPACE_SC_END

#endif
