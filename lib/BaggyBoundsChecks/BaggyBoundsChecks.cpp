//===- BaggyBoundChecks.cpp - Instrumentation for Baggy Bounds ------------ --//
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
#include "llvm/GlobalVariable.h"
#include "llvm/Value.h"
#include "llvm/Constants.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Support/TypeBuilder.h"

#include "safecode/BaggyBoundsChecks.h"
#include "safecode/Runtime/BBMetaData.h"

#include <iostream>
#include <set>
#include <string>
#include <functional>

static const unsigned SLOT_SIZE=4;
static const unsigned SLOT=16;

using namespace llvm;

namespace llvm {

// Identifier variable for the pass
char InsertBaggyBoundsChecks::ID = 0;

// Statistics

// Register the pass
static RegisterPass<InsertBaggyBoundsChecks> P("baggy bounds aligning", 
                                               "Baggy Bounds Transform");

//
// Function: findP2Size()
//
// Description:
//  Find the power-of-two size that is greater than or equal to the specified
//  size.  Note that we will round small sizes up to SLOT_SIZE.
//
// Inputs:
//  objectSize - The size of the original object in bytes.
//
// Return value:
//  The exponent of the required size rounded to a power of two.  For example,
//  if we need 8 (2^3) bytes, we'd return 3.
//
static inline unsigned
findP2Size (unsigned long objectSize) {
  unsigned int size = SLOT_SIZE;
  while (((unsigned int)(1u<<size)) < objectSize) {
    ++size;
  }

  return size;
}

//
// Description:
//  Define BBMetaData struct type using TypeBuilder template. So for global and stack
//  variables, we can use this type to record their metadata when padding and aligning 
//  them.
//
template<bool xcompile> class TypeBuilder<BBMetaData, xcompile> {
public:
  static  StructType* get(LLVMContext& context) {
    return StructType::get(
      TypeBuilder<types::i<32>, xcompile>::get(context),
      TypeBuilder<types::i<32>*, xcompile>::get(context),
      NULL);
   }
};

//
// Function: mustAdjustGlobalValue()
//
// Description:
//  This function determines whether the global value must be adjusted for
//  baggy bounds checking.
//
// Return value:
//  0 - The value does not need to be adjusted for baggy bounds checking.
//  Otherwise, a pointer to the value is returned.
//
GlobalVariable *
mustAdjustGlobalValue (GlobalValue * V) {
  //
  // Only modify global variables.  Everything else is left unchanged.
  //
  GlobalVariable * GV = dyn_cast<GlobalVariable>(V);
  if (!GV) return 0;

  //
  // Don't adjust a global which has an opaque type.
  //
  if (StructType * ST = dyn_cast<StructType>(GV->getType()->getElementType())) {
    if (ST->isOpaque()) {
      return 0;
    }
  }

  //
  // Don't modify external global variables or variables with no uses.
  // 
  if (GV->isDeclaration()) {
    return 0;
  }

  //
  // Don't bother modifying the size of metadata.
  //
  if (GV->hasSection()) return 0;
  if (GV->getSection() == "llvm.metadata") return 0;

  std::string name = GV->getName();
  if (strncmp(name.c_str(), "llvm.", 5) == 0) return 0;
  if (strncmp(name.c_str(), "baggy.", 6) == 0) return 0;
  if (strncmp(name.c_str(), "__poolalloc", 11) == 0) return 0;

  // Don't modify something created by FreeBSD's ASSYM macro
  if (name[name.length()-2] == 'w') return 0;

  // Don't modify globals in the exitcall section of the Linux kernel
  if (GV->getSection() == ".exitcall.exit") return 0;

  //
  // Don't modify globals that are not emitted into the final executable.
  //
  if (GV->hasAvailableExternallyLinkage()) return 0;
  return GV;
}

//
// Method: adjustGlobalValue()
//
// Description:
//  This method adjusts the size and alignment of a global variable to suit
//  baggy bounds checking.
//
void
InsertBaggyBoundsChecks::adjustGlobalValue (GlobalValue * V) {
  //
  // Only modify global variables.  Everything else is left unchanged.
  //
  GlobalVariable * GV = mustAdjustGlobalValue(V);
  if (!GV) return;

  //
  // Find the greatest power-of-two size that is larger than the object's
  // current size.
  //
  Type * GlobalType = GV->getType()->getElementType();
  unsigned long objectSize = TD->getTypeAllocSize(GlobalType);
  unsigned long adjustedSize = objectSize + sizeof(BBMetaData);
  unsigned int size = findP2Size (adjustedSize);

  //
  // Find the optimal alignment for the memory object.  Note that we can use
  // a larger alignment than needed.
  //
  unsigned int alignment = 1u << (size); 
  if (GV->getAlignment() > alignment) alignment = GV->getAlignment();

  //
  // Adjust the size and alignment of the memory object.  If the object size
  // is already a power-of-two, then just set the alignment.  Otherwise, add
  // fields to the memory object to make it sufficiently large.
  //
  if (adjustedSize == (unsigned)(1u<<size)) {
    GV->setAlignment(1u<<size); 
  } else {
    //
    // Create a structure type.  The first element will be the global memory
    // object; the second will be an array of bytes that will pad the size out;
    // the third will be the metadata for this object.
    //
    Type *Int8Type = Type::getInt8Ty(GV->getContext());
    Type *newType1 = ArrayType::get (Int8Type, (1u<<size) - adjustedSize);
    Type *metadataType = TypeBuilder<BBMetaData, false>::get(GV->getContext());
    StructType *newType = StructType::get(GlobalType, newType1, metadataType, NULL);

    //
    // Create a global initializer.  The first element has the initializer of
    // the original memory object, the second initializes the padding array, and 
    // the third initializes the object's metadata.
    //
    Constant *c = 0;
    if (GV->hasInitializer()) {
      std::vector<Constant *> vals(3);
      vals[0] = GV->getInitializer();
      vals[1] = Constant::getNullValue(newType1);
      vals[2] = Constant::getNullValue(metadataType);
      c = ConstantStruct::get(newType, vals);
    }

    //
    // Create the new global memory object with the correct alignment.
    //
    GlobalVariable *GV_new = new GlobalVariable (*(GV->getParent()),
                                                 newType,
                                                 GV->isConstant(),
                                                 GV->getLinkage(),
                                                 c,
                                                 "baggy." + GV->getName());
    GV_new->copyAttributesFrom (GV);
    GV_new->setAlignment(1u<<size);
    GV_new->takeName (GV);

    //
    // Store the object size information into the medadata.
    //
    Type *Int32Type = Type::getInt32Ty(GV->getContext());
    Value *Zero = ConstantInt::getSigned(Int32Type, 0);
    Value *Two = ConstantInt::getSigned(Int32Type, 2);
    Value *idx[3] = {Zero, Two, Zero};
    Value *V = GetElementPtrInst::Create(GV_new,idx, Twine(""));
    new StoreInst(ConstantInt::getSigned(Int32Type, objectSize), V);

    //
    // Create a GEP expression that will represent the global value and replace
    // all uses of the global value with the new constant GEP.
    //
    Value *idx1[2] = {Zero, Zero};
    Constant *init = ConstantExpr::getGetElementPtr(GV_new, idx1, 2);
    GV->replaceAllUsesWith(init);
    GV->eraseFromParent();
  }

  return;
}

//
// Method: adjustAlloca()
//
// Description:
//  Modify the specified alloca instruction (if necessary) to give it the
//  needed alignment and padding for baggy bounds checking.
//
void
InsertBaggyBoundsChecks::adjustAlloca (AllocaInst * AI) {
  //
  // Get the power-of-two size for the alloca.
  //
  unsigned objectSize = TD->getTypeAllocSize (AI->getAllocatedType());
  unsigned adjustedSize = objectSize + sizeof(BBMetaData);
  unsigned char size = findP2Size (adjustedSize);

  //
  // Adjust the size and alignment of the memory object.  If the object size
  // is already a power-of-two, then just set the alignment.  Otherwise, add
  // fields to the memory object to make it sufficiently large.
  //
  if (adjustedSize == (unsigned)(1u<<size)) {
    AI->setAlignment(1u<<size);
  } else {
    //
    // Create necessary types.
    //
    Type *Int8Type = Type::getInt8Ty (AI->getContext());
    Type *Int32Type = Type::getInt32Ty (AI->getContext());

    //
    // Create a structure type.  The first element will be the global memory
    // object; the second will be an array of bytes that will pad the size out;
    // the third will be the metadata for this object.
    //
    Type *newType1 = ArrayType::get(Int8Type, (1<<size) - adjustedSize);
    Type *metadataType = TypeBuilder<BBMetaData, false>::get(AI->getContext());
    
    StructType *newType = StructType::get(AI->getType()->getElementType(), newType1, metadataType, NULL);
    
    //
    // Create the new alloca instruction and set its alignment.
    //
    AllocaInst * AI_new = new AllocaInst (newType,
                                               0,
                                          (1<<size),
                                          "baggy." + AI->getName(),
                                          AI);
    AI_new->setAlignment(1u<<size);

    //
    // Store the object size information into the medadata.
    //
    Value *Zero = ConstantInt::getSigned(Int32Type, 0);
    Value *Two = ConstantInt::getSigned(Int32Type, 2);
    Value *idx[3] = {Zero, Two, Zero};
    Value *V = GetElementPtrInst::Create(AI_new, idx, Twine(""));
    new StoreInst(ConstantInt::getSigned(Int32Type, objectSize), V);

    //
    // Create a GEP that accesses the first element of this new structure.
    //
    //Value * Zero = ConstantInt::getSigned(Int32Type, 0);
    Value *idx1[2] = {Zero, Zero};
    Instruction *init = GetElementPtrInst::Create(AI_new,
                                                  idx1,
                                                  Twine(""),
                                                  AI);
    AI->replaceAllUsesWith(init);
    AI->removeFromParent(); 
    AI_new->setName(AI->getName());
  }

  return;
}

//
// Method: adjustAllocasFor()
//
// Description:
//  Look for allocas used in calls to the specified function and adjust their
//  size and alignment for baggy bounds checking.
//
void
InsertBaggyBoundsChecks::adjustAllocasFor (Function * F) {
  //
  // If there is no such function, do nothing.
  //
  if (!F) return;

  //
  // Scan through all uses of the function and process any allocas used by it.
  //
  for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {
    if (CallInst * CI = dyn_cast<CallInst>(*FU)) {
      Value * Ptr = CI->getArgOperand(1)->stripPointerCasts();
      if (AllocaInst * AI = dyn_cast<AllocaInst>(Ptr)){
        adjustAlloca (AI);
      } 
    }
  }

  return;
}

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
#if 1
  Type *Int8Type = Type::getInt8Ty(M.getContext());
  Type *Int32Type = Type::getInt32Ty(M.getContext());
#endif

  //
  // Align and pad global variables.
  //
  std::vector<GlobalVariable *> varsToTransform;
  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for (; GI != GE; ++GI) {
    if (GlobalVariable * GV = mustAdjustGlobalValue (GI))
      varsToTransform.push_back (GV);
  }

  for (unsigned index = 0; index < varsToTransform.size(); ++index) {
    adjustGlobalValue (varsToTransform[index]);
  }
  varsToTransform.clear();

  //
  // Align and pad stack allocations (allocas) that are registered with the
  // run-time.  We don't do all stack objects because we don't need to adjust
  // the size of an object that is never returned in a table lookup.
  //
  adjustAllocasFor (M.getFunction ("pool_register_stack"));
  adjustAllocasFor (M.getFunction ("pool_register_stack_debug"));

#if 1
  // changes for register argv
  Function *ArgvReg = M.getFunction ("poolargvregister");
  assert (ArgvReg && "FIXME: Should not assume that argvregister is used!");
  if (!ArgvReg->use_empty()) {
    assert (isa<PointerType>(ArgvReg->getReturnType()));
    assert (ArgvReg->getNumUses() == 1);
    CallInst *CI = cast<CallInst>(*(ArgvReg->use_begin()));
    Value *Argv = CI->getArgOperand(1);
    BasicBlock::iterator I = CI;
    I++;
    BitCastInst *BI = new BitCastInst(CI, Argv->getType(), "argv_temp",cast<Instruction>(I));
    std::vector<User *> Uses;
    Value::use_iterator UI = Argv->use_begin();
    for (; UI != Argv->use_end(); ++UI) {
      if (Instruction * Use = dyn_cast<Instruction>(*UI))
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

  //
  // align byval arguments
  //
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
        if(It->use_empty())
          continue;
        assert (isa<PointerType>(It->getType()));
        PointerType * PT = cast<PointerType>(It->getType());
        Type * ET = PT->getElementType();
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
            if (CallInst * CI = dyn_cast<CallInst>(*FU)) {
              if (CI->getCalledFunction() == &F) {
                CI->addAttribute(i, llvm::Attribute::constructAlignmentFromInt(1u<<size));
              }
            }
          } 
        } else {
          Type *newType1 = ArrayType::get(Int8Type, (alignment)-AllocSize);
          StructType *newSType = StructType::get(ET, newType1, NULL);

          FunctionType *FTy = F.getFunctionType();
          // Construct the new Function Type
          // Appends the struct Type at the beginning
          std::vector<Type*>TP;
          TP.push_back(newSType->getPointerTo());
          for(unsigned c = 0; c < FTy->getNumParams();c++) {
            TP.push_back(FTy->getParamType(c));
          }
          //return type is same as that of original instruction
          FunctionType *NewFTy = FunctionType::get(FTy->getReturnType(), TP, false);
          Function *NewF = Function::Create(NewFTy,
                                            GlobalValue::InternalLinkage,
                                            F.getNameStr() + ".TEST",
                                            &M);


          Function::arg_iterator NII = NewF->arg_begin();
          NII->setName("Baggy");
          ++NII;

          ValueToValueMapTy ValueMap;

          for (Function::arg_iterator II = F.arg_begin(); NII != NewF->arg_end(); ++II, ++NII) {
            ValueMap[II] = NII;
            NII->setName(II->getName());
          }
          // Perform the cloning.
          SmallVector<ReturnInst*,100> Returns;
          CloneFunctionInto(NewF, &F, ValueMap, false, Returns);
          std::vector<Value*> fargs;
          for(Function::arg_iterator ai = NewF->arg_begin(),
              ae= NewF->arg_end(); ai != ae; ++ai) {
            fargs.push_back(ai);
          }
          
          NII = NewF->arg_begin();

          Value *zero = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
          Value *Idx[] = { zero, zero };
          Instruction *InsertPoint;
          for (BasicBlock::iterator insrt = NewF->front().begin(); isa<AllocaInst>(InsertPoint = insrt); ++insrt) {;}

          GetElementPtrInst *GEPI = GetElementPtrInst::Create(cast<Value>(NII), Idx, Twine(""), InsertPoint);

          fargs.at(i)->replaceAllUsesWith(GEPI);
          Function::const_arg_iterator I = F.arg_begin(),E = F.arg_end();
          for (Function::const_arg_iterator I = F.arg_begin(), 
               E = F.arg_end(); I != E; ++I) {
            NewF->getAttributes().addAttr(I->getArgNo() + 1,  F.getAttributes().getParamAttributes(I->getArgNo() + 1));
          }

          NewF->setAttributes(NewF->getAttributes()
                              .addAttr(0, F.getAttributes()
                                       .getRetAttributes()));
          NewF->setAttributes(NewF->getAttributes()
                              .addAttr(~0, F.getAttributes()
                                       .getFnAttributes()));

          NewF->addAttribute(1, llvm::Attribute::constructAlignmentFromInt(1u<<size));
          NewF->addAttribute(1, F.getAttributes().getParamAttributes(i));

          // Change uses.
          for (Value::use_iterator FU = F.use_begin(); FU != F.use_end(); ) {
            if (CallInst * CI = dyn_cast<CallInst>(*FU++)) {
              if (CI->getCalledFunction() == &F) {
                Function *Caller = CI->getParent()->getParent();
                Instruction *InsertPoint;
                for (BasicBlock::iterator insrt = Caller->front().begin(); isa<AllocaInst>(InsertPoint = insrt); ++insrt) {;}
                AllocaInst *AINew = new AllocaInst(newSType, "", InsertPoint);
                LoadInst *LINew = new LoadInst(CI->getOperand(i), "", CI);
                GetElementPtrInst *GEPNew = GetElementPtrInst::Create(AINew, Idx, Twine(""), CI);
                new StoreInst(LINew, GEPNew, CI);
                SmallVector<Value*, 8> Args;
                Args.push_back(AINew);
                for(unsigned j =1;j<CI->getNumOperands();j++) {
                  Args.push_back(CI->getOperand(j));
                }
                CallInst *CallI = CallInst::Create(NewF, Args,"", CI);
                CallI->addAttribute(1, llvm::Attribute::constructAlignmentFromInt(1u<<size));
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
#endif
  return true;
}

}

