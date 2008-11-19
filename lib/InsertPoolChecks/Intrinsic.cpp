//===- Intrinsic.cpp - Insert declaration of SAFECode intrinsic to bc files --------------//
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

#include "llvm/Module.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"

#include "safecode/Intrinsic.h"
#include "safecode/VectorListHelper.h"

NAMESPACE_SC_BEGIN

#define REG_FUNC(type, name, ret,  ...) do { addIntrinsic(type, name, FunctionType::get(ret, args<const Type*>::list(__VA_ARGS__), false)); } while (0)

bool
InsertSCIntrinsic::runOnModule(Module & M) {
  currentModule = &M;
  static const Type * VoidTy = Type::VoidTy;
  static const Type * Int32Ty = Type::Int32Ty;
  static const Type * vpTy = PointerType::getUnqual(Type::Int8Ty);

  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.lscheck",		VoidTy, vpTy, vpTy);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.lscheckui",		VoidTy, vpTy, vpTy);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.lscheckalign",	VoidTy, vpTy, vpTy, Int32Ty);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.lscheckalignui",	VoidTy, vpTy, vpTy, Int32Ty);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.boundscheck",	VoidTy, vpTy, vpTy, vpTy);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.boundscheckui",	VoidTy, vpTy, vpTy, vpTy);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.exactcheck", 	VoidTy, Int32Ty, Int32Ty);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.exactcheck2", 	VoidTy, vpTy, vpTy, Int32Ty);
  REG_FUNC(SC_INTRINSIC_CHECK,	"sc.funccheck", 		VoidTy, Int32Ty, vpTy, vpTy);
  REG_FUNC(SC_INTRINSIC_OOB,		"sc.get_actual_val",	vpTy, vpTy, vpTy);

  return true;
}

void
InsertSCIntrinsic::addIntrinsic(IntrinsicType type, const std::string & name, FunctionType * FTy)
{
  IntrinsicInfoTy info;
  info.type = type;
  info.F = dyn_cast<Function>(currentModule->getOrInsertFunction(name, FTy));
  intrinsicNameMap.insert(std::make_pair<std::string, IntrinsicInfoTy>(name, info));
  intrinsicFuncMap.insert(std::make_pair<Function*, IntrinsicInfoTy>(info.F, info));
}

const InsertSCIntrinsic::IntrinsicInfoTy &
InsertSCIntrinsic::getIntrinsic(const std::string & name) const {
  std::map<std::string, IntrinsicInfoTy>::const_iterator it = intrinsicNameMap.find(name);
  assert(it != intrinsicNameMap.end() && "Intrinsic should be defined before uses!");
  return it->second;
}

bool
InsertSCIntrinsic::isSCIntrinsic(Value * inst) const {
  if (!isa<CallInst>(inst)) 
    return false;

  CallInst * CI = dyn_cast<CallInst>(inst);
  Function * F= CI->getCalledFunction();
  if (!F)
    return false;

  return intrinsicFuncMap.find(F) != intrinsicFuncMap.end();
}

bool
InsertSCIntrinsic::isCheckingIntrinsic(Value * inst) const {
  if (!isSCIntrinsic(inst))
    return false;

  CallInst * CI = dyn_cast<CallInst>(inst);
  std::string name = CI->getCalledFunction()->getName();
  const IntrinsicInfoTy & info = getIntrinsic(name);
  return info.type == SC_INTRINSIC_CHECK;
}

char InsertSCIntrinsic::ID = 0;
static llvm::RegisterPass<InsertSCIntrinsic> X ("sc-insert-intrinsic", "insert SAFECode's intrinsic");

NAMESPACE_SC_END
