//===- BaggyBoundChecks.cpp - Instrumentation for Baggy Bounds -------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass aligns globals and stack allocated values to the correct power to 
// two boundary.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "baggy-bound-checks"

#include "llvm/ADT/Statistic.h"
#include "llvm/Constants.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Support/InstIterator.h"

#include "safecode/BaggyBoundsChecks.h"
#include "SCUtils.h"

#include <iostream>
#include <string>
#include <functional>
using namespace llvm;

NAMESPACE_SC_BEGIN
#define SLOT_SIZE 4
// Identifier variable for the pass
char InsertBaggyBoundsChecks::ID = 0;

// Statistics

// Register the pass
static RegisterPass<InsertBaggyBoundsChecks> P ("baggy bounds aligning", 
                                               "Baggy Bounds Transform");

//
// Method: runOnModule()
//
// Description:
//  Entry point for this LLVM pass.
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
InsertBaggyBoundsChecks::runOnModule (Module & M) {
  // Get prerequisite analysis resuilts.
  TD = &getAnalysis<TargetData>();
  intrinsicPass = &getAnalysis<InsertSCIntrinsic>();
  const Type *Int8Type = Type::getInt8Ty(getGlobalContext());
  const Type *Int32Type = Type::getInt32Ty(getGlobalContext());

  // align globals, and pad 
  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    GlobalVariable *GV = dyn_cast<GlobalVariable>(GI);
    if (!GV) continue;
    if (GV->isDeclaration()) {
      // Don't bother to register external global variables
      continue;
    }
    if (GV->getNumUses() == 0) continue;
    if ((GV->getSection()) == "llvm.metadata") continue;
    std::string name = GV->getName();
    if (strncmp(name.c_str(), "llvm.", 5) == 0) continue;
    if (strncmp(name.c_str(), "baggy.", 6) == 0) continue;
    if (strncmp(name.c_str(), "__poolalloc", 11) == 0) continue;

    if (SCConfig.svaEnabled()) {
      // Linking fails when registering objects in section exitcall.exit
      if (GV->getSection() == ".exitcall.exit") continue;
    }
      
    const Type * GlobalType = GV->getType()->getElementType();
    unsigned long int i = TD->getTypeAllocSize(GlobalType);
                        
    unsigned char size = (unsigned char)ceil(log(i)/log(2));
    uintptr_t alignment = 1 << size; 
    /*if(GV->getAlignment() && alignment %(GV->getAlignment())) { 
     alignment = GV->getAlignment();
    } */
    if(alignment < 16) alignment = 16;
    if(GV->getAlignment() > alignment) alignment = GV->getAlignment();
      if(i == alignment) {
        GV->setAlignment(alignment);
      } 
      else {
        GV->dump();
        Type *newType1 = ArrayType::get(Int8Type, (alignment)-i);
        StructType *newType = StructType::get(getGlobalContext(), GlobalType, newType1, NULL);
        std::vector<Constant *> vals(2);
        vals[0] = GV->getInitializer();
        vals[1] = Constant::getNullValue(newType1);
        Constant *c = ConstantStruct::get(newType, vals);
        GlobalVariable *GV_new = new GlobalVariable(M, newType, GV->isConstant(), GV->getLinkage(),c, "");
        GV_new->setAlignment(alignment);
        Constant *Zero= ConstantInt::getSigned(Int32Type, 0);
        Constant *idx[2] = {Zero, Zero};
        Constant *init = ConstantExpr::getGetElementPtr(GV_new, idx, 2);
        GV->replaceAllUsesWith(init);
        GV->setName("");
        GV_new->setName(name);
        GV_new->dump();
      } 
    }
    
  //align allocas
  Function *F = intrinsicPass->getIntrinsic("sc.pool_register_stack").F;  
  for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {

    if (CallInst * CI = dyn_cast<CallInst>(FU)) {
      std::set<Value *>Chain;
      Value * RealOperand = intrinsicPass->getValuePointer (CI);
      Value * PeeledOperand = peelCasts (RealOperand, Chain);
      AllocaInst *AI = cast<AllocaInst>(PeeledOperand);
      unsigned i = TD->getTypeAllocSize(AI->getAllocatedType());
      unsigned char size = (unsigned char)ceil(log(i)/log(2)); 
      if(size < 4) {
        size = 4;
      }
      if(i == (unsigned)(1<<size)) {
        AI->setAlignment(1<<size);
      } else if(size == 4) {
        AI->setAlignment(16);
      } else {
        BasicBlock::iterator InsertPt1 = AI;
        Instruction * iptI1 = ++InsertPt1;
        Type *newType1 = ArrayType::get(Int8Type, (1<<size)-i);
        StructType *newType = StructType::get(getGlobalContext(), AI->getType()->getElementType(), newType1, NULL);
        AllocaInst * AI_new = new AllocaInst(newType, 0,(1<<size) , "baggy."+AI->getName(), iptI1);
        AI_new->setAlignment(1<<size);
        Value *Zero= ConstantInt::getSigned(Int32Type, 0);
        Value *idx[3]= {Zero, Zero, NULL};
        Instruction *init = GetElementPtrInst::Create(AI_new, idx + 0, idx + 1, Twine(""), iptI1);
        init = GetElementPtrInst::Create(init, idx + 0, idx + 2, Twine(""), iptI1);
        AI->replaceAllUsesWith(init);
        AI->removeFromParent(); 
        AI_new->setName(AI->getName());
        //new AllocaInst(Int8Type->getPointerTo(), ConstantInt::get(Type::getInt32Ty(getGlobalContext()),((1<<size) - i)), "baggy."+AI->getName(), iptI1); 
      } 
    }
  }

  // changes for register argv
  Function *ArgvReg = intrinsicPass->getIntrinsic("sc.pool_argvregister").F;  
  ArgvReg->dump();  
  if (ArgvReg->getNumUses() == 0){
    return true;
  }
  assert (isa<PointerType>(ArgvReg->getReturnType()));
  assert (ArgvReg->getNumUses() == 1);
  CallInst *CI = cast<CallInst>(ArgvReg->use_begin()); 
  Value *Argv = intrinsicPass->getValuePointer (CI);
  Argv->dump();
  BasicBlock::iterator I = CI;
  I++;
  BitCastInst *BI = new BitCastInst(CI, Argv->getType(), "argv_temp",cast<Instruction>(I));
  BI->dump();
  std::vector<User *> Uses;
  Value::use_iterator UI = Argv->use_begin();
  for (; UI != Argv->use_end(); ++UI) {
    if (Instruction * Use = dyn_cast<Instruction>(UI))
      if (CI != Use) {
        Uses.push_back (*UI);
      }
  }
  
  while (Uses.size()) {
    User * Use = Uses.back();
    Uses.pop_back();
    Use->replaceUsesOfWith (Argv, BI);
  }
  
  return true;
}

NAMESPACE_SC_END

