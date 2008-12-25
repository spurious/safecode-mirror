//===- Intrinsic.h - Insert declaration of SAFECode intrinsic to bc files --------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a module plass to insert declaration of
// SAFECode intrinsic to the bitcode file. It also provides interfaces
// for latter passes to use these intrinsics.
//
//===----------------------------------------------------------------------===//

#ifndef _SC_INTRINSIC_H_
#define _SC_INTRINSIC_H_

#include "safecode/SAFECode.h"

#include <set>
#include <map>

#include "llvm/Pass.h"

NAMESPACE_SC_BEGIN

class InsertSCIntrinsic : public llvm::ModulePass {
 public:
  static char ID;
  typedef enum IntrinsicType {
    SC_INTRINSIC_NO_OP,
    SC_INTRINSIC_CHECK,
    SC_INTRINSIC_OOB,
    SC_INTRINSIC_POOL_CONTROL,
    SC_INTRINSIC_COUNT
  } IntrinsicType;

  typedef struct IntrinsicInfo {
    IntrinsicType type;
    llvm::Function * F;
  } IntrinsicInfoTy;

 InsertSCIntrinsic() : llvm::ModulePass((intptr_t)&ID), currentModule(NULL) {}
  virtual ~InsertSCIntrinsic() {}
  virtual bool runOnModule(llvm::Module & M);
  virtual const char * getPassName() const { return "Insert declaration of SAFECode Intrinsic"; }
  void addIntrinsic(IntrinsicType type, const std::string & name, llvm::FunctionType * FTy);
  const IntrinsicInfoTy & getIntrinsic(const std::string & name) const;
  bool isSCIntrinsic(llvm::Value * inst) const;
  bool isCheckingIntrinsic(llvm::Value * inst) const;
  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    AU.setPreservesAll();
  }
 private:
  llvm::Module * currentModule;
  std::map<llvm::Function *, IntrinsicInfoTy> intrinsicFuncMap;
  std::map<std::string, IntrinsicInfoTy> intrinsicNameMap;
};

NAMESPACE_SC_END

#endif
