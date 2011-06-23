//===------------ FormatStrings.h - Secure format string function calls ---===//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass finds calls to format string functions and replaces them with
// secure runtime wrapper calls.
//
//===----------------------------------------------------------------------===//

#ifndef FORMAT_STRINGS_H
#define FORMAT_STRINGS_H

#include "llvm/Module.h"
#include "llvm/Pass.h"

#include "safecode/Intrinsic.h"
#include "safecode/SAFECode.h"

#include <map>
#include <set>
#include <utility>

using std::map;
using std::set;
using std::pair;

using namespace llvm;

NAMESPACE_SC_BEGIN

class FormatStringTransform : public ModulePass
{

private:

  Value *FSParameter;
  Value *FSCallInfo;
  const Type *PointerInfoType;

  map<Function *, Instruction *> CallInfoStructures;
  map<Function *, Instruction *> PointerInfoStructures;

  typedef pair<Instruction *, Value *> PointerInfoForParameter;
  map<PointerInfoForParameter, Value *> FSParameterCalls;

  map<Instruction *, unsigned> PointerInfoArrayUsage;
  map<Function *, unsigned> PointerInfoFuncArrayUsage;
  map<Function *, unsigned> CallInfoStructUsage;

  const Type *makePointerInfoType(LLVMContext &Context);

  const Type *makeCallInfoType(LLVMContext &Context, unsigned argc);

  FunctionType *buildTransformedFunctionType(LLVMContext &C,
                                             unsigned argc,
                                             const FunctionType *F);

  void fillArraySizes(Module &M);

  bool transform(Module &M,
                 const char *name,
                 unsigned argc,
                 const char *replacement,
                 Statistic &stat);

  Value *registerPointerParameter(Instruction *i, Value *Parameter);

  Value *registerCallInformation(Instruction *i,
                                 uint32_t vargc,
                                 const set<Value *> &PointerVArguments);

  CallInst *buildSecuredCall(Value *newFunc,
                             CallInst &oldCall);

public:

  static char ID;

  FormatStringTransform() : ModulePass((intptr_t) &ID) {}

  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const
  {
    AU.addRequired<InsertSCIntrinsic>();
  }

};

NAMESPACE_SC_END

#endif
