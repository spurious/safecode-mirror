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
// FIXME: Alignment for Global Variables in LLVM 2.7 is a 16 bit field, and thus 
// setting alignments larger than 2^16 fails. Have hacked it to work, by changing 
// the alignment field to 32 bit in LLVM_SRC/include/llvm/GlobalValue.h.
// Should work fine on LLVM 2.8. 
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "baggy-bound-checks"

#include "llvm/ADT/Statistic.h"
#include "llvm/Value.h"
#include "llvm/Constants.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "safecode/BaggyBoundsChecks.h"
#include "SCUtils.h"

#include <iostream>
#include <string>
#include <functional>
using namespace llvm;

NAMESPACE_SC_BEGIN
#define SLOT_SIZE 4
#define SLOT 16
// Identifier variable for the pass
char InsertBaggyBoundsChecks::ID = 0;

// Statistics

// Register the pass
static RegisterPass<InsertBaggyBoundsChecks> P("baggy bounds aligning", 
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
    if (GV->getSection() == "llvm.metadata") continue;

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
    unsigned int size= 0;
    while((unsigned int)(1u<<size) < i) {
      size++;
    }
    if(size < SLOT_SIZE) {
      size = SLOT_SIZE;
    }

    unsigned int alignment = 1u << (size); 
    if(GV->getAlignment() > alignment) alignment = GV->getAlignment();
    if(i == (unsigned)(1u<<size)) {
      GV->setAlignment(1u<<size); 
    } else {
      Type *newType1 = ArrayType::get(Int8Type, (alignment)-i);
      StructType *newType = StructType::get(getGlobalContext(), GlobalType, newType1, NULL);
      std::vector<Constant *> vals(2);
      vals[0] = GV->getInitializer();
      vals[1] = Constant::getNullValue(newType1);
      Constant *c = ConstantStruct::get(newType, vals);
      GlobalVariable *GV_new = new GlobalVariable(M, newType, GV->isConstant(), GV->getLinkage(),c, "baggy."+GV->getName());
      GV_new->setAlignment(1u<<size);
      Constant *Zero= ConstantInt::getSigned(Int32Type, 0);
      Constant *idx[2] = {Zero, Zero};
      Constant *init = ConstantExpr::getGetElementPtr(GV_new, idx, 2);
      GV->replaceAllUsesWith(init);
    } 
  }

  //align allocas
  Function *F = intrinsicPass->getIntrinsic("sc.pool_register_stack").F;  
  for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {

    if (CallInst * CI = dyn_cast<CallInst>(FU)) {
      std::set<Value *>Chain;
      Value * RealOperand = intrinsicPass->getValuePointer (CI);
      Value * PeeledOperand = peelCasts(RealOperand, Chain);
      if(!isa<AllocaInst>(PeeledOperand)){
        continue;
      }
      AllocaInst *AI = cast<AllocaInst>(PeeledOperand);
      unsigned i = TD->getTypeAllocSize(AI->getAllocatedType());
      unsigned char size = 0;
      while((unsigned)(1<<size) < i) {
        size++;
      }

      if(size < SLOT_SIZE) {
        size = SLOT_SIZE;
      }

      if(i == (unsigned)(1u<<size)) {
        AI->setAlignment(1u<<size);
      } else {
        BasicBlock::iterator InsertPt1 = AI;
        Instruction * iptI1 = ++InsertPt1;
        Type *newType1 = ArrayType::get(Int8Type, (1<<size)-i);
        StructType *newType = StructType::get(getGlobalContext(), AI->getType()->getElementType(), newType1, NULL);
        AllocaInst * AI_new = new AllocaInst(newType, 0,(1<<size) , "baggy."+AI->getName(), iptI1);
        AI_new->setAlignment(1u<<size);
        Value *Zero= ConstantInt::getSigned(Int32Type, 0);
        Value *idx[3] = {Zero, Zero, NULL};
        Instruction *init = GetElementPtrInst::Create(AI_new, idx + 0, idx + 1, Twine(""), iptI1);
        init = GetElementPtrInst::Create(init, idx + 0, idx + 2, Twine(""), iptI1);
        AI->replaceAllUsesWith(init);
        AI->removeFromParent(); 
        AI_new->setName(AI->getName());
      } 
    }
  }

  // changes for register argv
  Function *ArgvReg = intrinsicPass->getIntrinsic("sc.pool_argvregister").F;  
  if (!ArgvReg->use_empty()) {
    assert (isa<PointerType>(ArgvReg->getReturnType()));
    assert (ArgvReg->getNumUses() == 1);
    CallInst *CI = cast<CallInst>(ArgvReg->use_begin()); 
    Value *Argv = intrinsicPass->getValuePointer (CI);
    BasicBlock::iterator I = CI;
    I++;
    BitCastInst *BI = new BitCastInst(CI, Argv->getType(), "argv_temp",cast<Instruction>(I));
    std::vector<User *> Uses;
    Value::use_iterator UI = Argv->use_begin();
    for (; UI != Argv->use_end(); ++UI) {
      if (Instruction * Use = dyn_cast<Instruction>(UI))
        if (CI != Use) {
          Uses.push_back (*UI);
        }
    }

    while (Uses.size()) {
      User *Use = Uses.back();
      Uses.pop_back();
      Use->replaceUsesOfWith (Argv, BI);
    }
  }

  // align byval arguments
  //FIXME Alignment does not work for byval arguments on x86_64(see LLVM Bug 6965, 9637)


  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++ I) {
    if (I->isDeclaration()) continue;

    if (I->hasName()) {
      std::string Name = I->getName();
      if ((Name.find ("__poolalloc") == 0) || (Name.find ("sc.") == 0)
          || Name.find("baggy.") == 0)
        continue;
    }
    Function &F = cast<Function>(*I);
    unsigned int i = 1;
    for (Function::arg_iterator It = F.arg_begin(), E = F.arg_end(); It != E; ++It, ++i) {
      if (It->hasByValAttr()) {
        assert (isa<PointerType>(It->getType()));
        const PointerType * PT = cast<PointerType>(It->getType());
        const Type * ET = PT->getElementType();
        unsigned  AllocSize = TD->getTypeAllocSize(ET);
        unsigned char size= 0;
        while((unsigned)(1u<<size) < AllocSize) {
          size++;
        }
        if(size < SLOT_SIZE) {
          size = SLOT_SIZE;
        }

        unsigned int alignment = 1u << size;
        if(AllocSize == alignment) {
          F.addAttribute(i, llvm::Attribute::constructAlignmentFromInt(1u<<size));
          for (Value::use_iterator FU = F.use_begin(); FU != F.use_end(); ++FU) {
            if (CallInst * CI = dyn_cast<CallInst>(FU)) {
              if (CI->getCalledFunction() == &F) {
                CI->addAttribute(i, llvm::Attribute::constructAlignmentFromInt(1u<<size));
              }
            }
          } 
        } else {
          Type *newType1 = ArrayType::get(Int8Type, (alignment)-AllocSize);
          StructType *newSType = StructType::get(getGlobalContext(), ET, newType1, NULL);

          const FunctionType *FTy = F.getFunctionType();
          // Construct the new Function Type
          // Appends the struct Type at the beginning
          std::vector<const Type*>TP;
          for(unsigned c = 0; c < FTy->getNumParams();c++) {
            if(c == (i-1))
              TP.push_back(newSType->getPointerTo());
            else 
              TP.push_back(FTy->getParamType(c));
          }
          //return type is same as that of original instruction
          const FunctionType *NewFTy = FunctionType::get(FTy->getReturnType(), TP, false);
          Function *NewF = Function::Create(NewFTy,
                                            GlobalValue::InternalLinkage,
                                            F.getNameStr() + ".TEST",
                                            &M);


          NewF->copyAttributesFrom(&F);
          NewF->setAttributes(NewF->getAttributes()
                              .addAttr(0, F.getAttributes()
                                       .getRetAttributes()));
          NewF->setAttributes(NewF->getAttributes()
                              .addAttr(~0, F.getAttributes()
                                       .getFnAttributes()));

          Function::arg_iterator NI = NewF->arg_begin();

          DenseMap<const Value*, Value*> ValueMap;

          for (Function::arg_iterator II = F.arg_begin(); NI != NewF->arg_end(); ++II, ++NI) {
            ValueMap[II] = NI;
            NI->setName(II->getName());
          }
          // Perform the cloning.
          SmallVector<ReturnInst*,100> Returns;
          CloneFunctionInto(NewF, &F, ValueMap, Returns);
          NewF->addAttribute(i, llvm::Attribute::constructAlignmentFromInt(1u<<size));
          
          // Change uses.
          for (Value::use_iterator FU = F.use_begin(); FU != F.use_end(); ) {
            if (CallInst * CI = dyn_cast<CallInst>(FU++)) {
              if (CI->getCalledFunction() == &F) {
                Function *Caller = CI->getParent()->getParent();
                Instruction *InsertPoint;
                for (BasicBlock::iterator insrt = Caller->front().begin(); isa<AllocaInst>(InsertPoint = insrt); ++insrt) {;}
                AllocaInst *AINew = new AllocaInst(newSType, "", InsertPoint);
                LoadInst *LINew = new LoadInst(CI->getOperand(i), "", CI);
                Value *zero = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
                Value *Idx[] = { zero, zero };
                GetElementPtrInst *GEPNew = GetElementPtrInst::Create(AINew,Idx ,Idx+2,  "", CI);
                new StoreInst(LINew, GEPNew, CI);
                SmallVector<Value*, 8> Args;
                for(unsigned j =1;j<CI->getNumOperands();j++) {
                  if(j == i) 
                    Args.push_back(AINew);
                  else 
                    Args.push_back(CI->getOperand(j));
                }
                CallInst *CallI = CallInst::Create(NewF,Args.begin(), Args.end(),"", CI);
                CallI->setAttributes(CI->getAttributes());
                CallI->addAttribute(i, llvm::Attribute::constructAlignmentFromInt(1u<<size));
                CallI->setCallingConv(CI->getCallingConv());
                CI->replaceAllUsesWith(CallI);
                CI->eraseFromParent();
              }
            }
          } 
        }

      }
    }
  }
  return true;
}

NAMESPACE_SC_END

