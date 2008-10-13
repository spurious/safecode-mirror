//===- AllocaWithAggregateTypes.cpp - Initialize allocas with pointers -------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that ensures that uninitialized memory created
// by alloca instructions is not used to violate memory safety.  It can do this
// in one of two ways:
//
//   o) Promote the allocations from stack to heap.
//   o) Insert code to initialize the newly allocated memory.
//
// The current implementation implements the latter, but code for the former is
// available but disabled.
//
//===----------------------------------------------------------------------===//

#include "safecode/Config/config.h"
#include "ConvertUnsafeAllocas.h"
#include "SCUtils.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Pass.h"
#include "llvm/BasicBlock.h"
#include "llvm/Type.h"
#include "llvm/Function.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/Debug.h"

#include <iostream>

using namespace llvm;

namespace {
  STATISTIC (InitedAllocas, "Allocas Initialized");
}

namespace llvm
{
#ifdef LLVA_KERNEL
static const unsigned meminitvalue = 0x00;
#else
#if defined(__linux__)
static const unsigned meminitvalue = 0xcc;
#else
static const unsigned meminitvalue = 0x00;
#endif
#endif

  inline bool
  InitAllocas::TypeContainsPointer(const Type *Ty) {
    if (Ty->getTypeID() == Type::PointerTyID)
      return true;
    else if (Ty->getTypeID() == Type::StructTyID) {
      const StructType * structTy = cast<StructType>(Ty);
      unsigned numE = structTy->getNumElements();
      for (unsigned index = 0; index < numE; index++) {
        if (TypeContainsPointer(structTy->getElementType(index)))
          return true;
      }
    } else if (Ty->getTypeID() == Type::ArrayTyID) {
      const ArrayType *arrayTy = cast<ArrayType>(Ty);
      if (TypeContainsPointer(arrayTy->getElementType()))
        return true;
    }
    return false;
  }

  //
  // FIXME:
  //  I don't think the code below is strictly correct.  I think we could have
  //  a type-known node that allocates memory using non-pointer LLVM types but
  //  uses the memory as a structure with a pointer in a type consistent
  //  manner.
  //
  //    Example:
  //
  //    foo = alloc (unsigned char array[24]);
  //    ...
  //    (struct bar *)(foo)->pointer = p;
  //
  //  I think what we really want to do is to see if the DSNode for the given
  //  alloca is type-known *and* whether it has any links to other DSNodes.
  //
  inline bool
  InitAllocas::changeType (DSGraph & TDG, Instruction * Inst) {
    // Get the DSNode for this instruction
    DSNode *Node = TDG.getNodeForValue((Value *)Inst).getNode();

    //
    // Do not bother to change this allocation if the type is unknown;
    // regular SAFECode checks will prevent anything bad from happening to
    // uninitialzed pointers loaded from this memory.
    //
    if (Node && (Node->isNodeCompletelyFolded()))
      return false;

    //
    // Check to see if the instruction is an alloca.
    //
    if (Inst->getOpcode() == Instruction::Alloca) {
      AllocationInst * AllocInst = cast<AllocationInst>(Inst);
      
      //
      // Get the type of object allocated.
      //
      const Type * TypeCreated = AllocInst->getAllocatedType ();
      
      if (TypeContainsPointer(TypeCreated))
        return true;
      
    }
    return false;
  }

  bool
  InitAllocas::doInitialization (Module & M) {
    Type * VoidPtrType = PointerType::getUnqual(Type::Int8Ty);
    memsetF = M.getOrInsertFunction ("memset", Type::VoidTy,
                                               VoidPtrType,
                                               Type::Int32Ty,
                                               Type::Int32Ty, NULL);

    return true;
  }

  bool
  InitAllocas::runOnFunction (Function &F) {
    bool modified = false;
    Type * VoidPtrType = PointerType::getUnqual(Type::Int8Ty);

    // Don't bother processing external functions
    if ((F.isDeclaration()) || (F.getName() == "poolcheckglobals"))
      return modified;

    // Get references to previous analysis passes
    TargetData &TD = getAnalysis<TargetData>();
    TDDataStructures & TDPass = getAnalysis<TDDataStructures>();

    // Get the DSGraph for this function
    DSGraph & TDG = TDPass.getDSGraph(F);

    for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I) {
      for (BasicBlock::iterator IAddrBegin=I->begin(), IAddrEnd = I->end();
           IAddrBegin != IAddrEnd;
           ++IAddrBegin) {
        //
        // Determine if the instruction needs to be changed.
        //
        if (changeType (TDG, IAddrBegin)) {
          AllocationInst * AllocInst = cast<AllocationInst>(IAddrBegin);
#if 0
          //
          // Get the type of object allocated.
          //
          const Type * TypeCreated = AllocInst->getAllocatedType ();

          MallocInst * TheMalloc = 
          new MallocInst(TypeCreated, 0, AllocInst->getName(), 
          IAddrBegin);
          std::cerr << "Found alloca that is struct or array" << std::endl;

          //
          // Remove old uses of the old instructions.
          //
          AllocInst->replaceAllUsesWith (TheMalloc);

          //
          // Remove the old instruction.
          //
          AllocInst->getParent()->getInstList().erase(AllocInst);
          modified = true;
#endif
          // Insert object registration at the end of allocas.
          Instruction * iptI = ++IAddrBegin;
          --IAddrBegin;
          if (AllocInst->getParent() == (&(AllocInst->getParent()->getParent()->getEntryBlock()))) {
            BasicBlock::iterator InsertPt = AllocInst->getParent()->begin();
            while (&(*(InsertPt)) != AllocInst)
              ++InsertPt;
            while (isa<AllocaInst>(InsertPt))
              ++InsertPt;
            iptI = InsertPt;
          }

          // Create a value that calculates the alloca's size
          const Type * AllocaType = AllocInst->getAllocatedType();
          Value *AllocSize = ConstantInt::get (Type::Int32Ty,
                                               TD.getABITypeSize(AllocaType));

          if (AllocInst->isArrayAllocation())
            AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
                                               AllocInst->getOperand(0),
                                               "sizetmp",
                                               iptI);

          Value * TheAlloca = castTo (AllocInst, VoidPtrType, "cast", iptI);

          std::vector<Value *> args(1, TheAlloca);
          args.push_back (ConstantInt::get(Type::Int32Ty, meminitvalue));
          args.push_back (AllocSize);
          CallInst::Create (memsetF, args.begin(), args.end(), "", iptI);
          ++InitedAllocas;
        }
      }
    }
    return modified;
  }
}

namespace {
  RegisterPass<InitAllocas> Z("initallocas",
                              "Initialize stack allocations with pointers");
} // end of namespace

