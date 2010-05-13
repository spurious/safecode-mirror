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

#define DEBUG_TYPE "init-allocas"

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

//
// Basic LLVM Types
//
static const Type * VoidType  = 0;
static const Type * Int8Type  = 0;
static const Type * Int32Type = 0;

NAMESPACE_SC_BEGIN
  //
  // Constant: meminitvalue
  //
  // Description:
  //  This is the byte value that is used to initialize newly allocated memory.
  //  Pointers will be initialized to this value concatenated 4 times
  //  e.g., 0xcccccccc.
  //
  //  On Linux, we use 0xcccccccc because it is an address within the kernel
  //  address space that is inaccessible by user-space programs.  In all other
  //  circumstances, we use 0x00000000 (which is unmapped in most kernels and
  //  operating systems).
  //
#ifdef LLVA_KERNEL
  static const unsigned char meminitvalue = 0x00;
#else
#if defined(__linux__)
  static const unsigned char meminitvalue = 0xcc;
#else
  static const unsigned char meminitvalue = 0x00;
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
  // Method: changeType()
  //
  // Description:
  //  Determine whether the specified instruction is an allocation instruction
  //  that needs to have its result initialized.
  //
  // Inputs:
  //  Inst - An instruction that may be an allocation instruction.
  //
  // Results:
  //  true  - This is an allocation instruction that contains pointers; it
  //          requires initialization.
  //  false - This is either not an allocation instruction or an allocation
  //          instruction that does not require initialization.
  //
  // Notes:
  //  1) An allocation does not need initialization if it contains no pointers
  //     or is type-unknown (being type-unknown causes SAFECode to place
  //     load/store checks on the pointers loaded from the memory, so no
  //     initialization is needed).
  //
  //  2) We get the type of the allocated memory from DSA; we do not use the
  //     LLVM type of the allocation.  This is because a program can allocate
  //     memory using a type that contains no pointer but uses the memory
  //     consistently as a type with pointers.  For example, consider the
  //     following code:
  //
  //      foo = alloc (unsigned char array[24]);
  //      ...
  //      (struct bar *)(foo)->pointer = p;
  //
  inline bool
  InitAllocas::changeType (Instruction * Inst) {
    //
    // Only initialize alloca instructions.
    //
    if (isa<AllocaInst>(Inst)) {
      // Get the DSNode for this instruction
      DSGraph * DSG = dsaPass->getDSGraph (*(Inst->getParent()->getParent()));
      DSNode *Node = DSG->getNodeForValue (Inst).getNode();
      assert (Node && "Alloca has no DSNode!\n");

      //
      // Do not bother to change this allocation if the type is unknown;
      // regular SAFECode checks will prevent anything bad from happening to
      // uninitialzed pointers loaded from this memory.
      //
      if (Node->isNodeCompletelyFolded())
        return false;

      //
      // If we do not know everything that happens to the pointer (i.e., it is
      // incomplete or comes from external code), then go ahead and assume that
      // a pointer is within it somewhere.
      //
      if (Node->isIncompleteNode())
        return true;

      //
      // Scan through all types associated with the DSNode to determine if
      // it contains a type that contains a pointer.
      //
      DSNode::type_iterator tyi;
      for (tyi = Node->type_begin(); tyi != Node->type_end(); ++tyi) {
        for (svset<const Type*>::const_iterator tyii = tyi->second->begin(),
               tyee = tyi->second->end(); tyii != tyee; ++tyii) {
          //
          // Get the type of object allocated.  If there is no type, then it is
          // implicitly of void type.
          //
          const Type * TypeCreated = *tyii;
          if (!TypeCreated) {
            return false;
          }

          //
          // If the type contains a pointer, it must be changed.
          //
          if (TypeContainsPointer(TypeCreated))
            return true;
        }
      }

      //
      // We have scanned through all types associated with the pointer, and
      // none of them contains a pointer.
      //
      return false;
    }

    //
    // Type type contains no pointer.
    //
    return false;
  }

  bool
  InitAllocas::doInitialization (Module & M) {
    //
    // Create needed LLVM types.
    //
    VoidType  = Type::getVoidTy(getGlobalContext());
    Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
    Int32Type = IntegerType::getInt32Ty(getGlobalContext());
    Type * VoidPtrType = PointerType::getUnqual(Int8Type);

    //
    // Add the memset function to the program.
    //
    memsetF = M.getOrInsertFunction ("llvm.memset.i32", VoidType,
                                               VoidPtrType,
                                               Int8Type,
                                               Int32Type,
                                               Int32Type,
                                               NULL);

    return true;
  }

  bool
  InitAllocas::runOnFunction (Function &F) {
    bool modified = false;

    // Don't bother processing external functions
    if ((F.isDeclaration()) || (F.getName() == "poolcheckglobals"))
      return modified;

    // Get references to previous analysis passes
    dsaPass = &getAnalysis<EQTDDataStructures>();

    for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I) {
      for (BasicBlock::iterator IAddrBegin=I->begin(), IAddrEnd = I->end();
           IAddrBegin != IAddrEnd;
           ++IAddrBegin) {
        //
        // Skip any instruction that is not a stack allocation.
        //
        AllocaInst * AI = dyn_cast<AllocaInst>(IAddrBegin);
        if (!AI) continue;

        //
        // Determine if the instruction needs to be changed.
        //
        if (changeType (IAddrBegin)) {
          AllocaInst * AllocInst = cast<AllocaInst>(IAddrBegin);
          //
          // Create an aggregate zero value to initialize the alloca.
          //
          const Type * AllocedType = AllocInst->getAllocatedType();
          Constant * Init = Constant::getNullValue (AllocedType);

          //
          // Scan for a place to insert the instruction to initialize the
          // allocated memory.
          //
          Instruction * iptI = ++IAddrBegin;
          --IAddrBegin;
          BasicBlock & entryBlock = F.getEntryBlock();
          if (AllocInst->getParent() == (&entryBlock)) {
            BasicBlock::iterator InsertPt = AllocInst->getParent()->begin();
            while (&(*(InsertPt)) != AllocInst)
              ++InsertPt;
            while (isa<AllocaInst>(InsertPt))
              ++InsertPt;
            iptI = InsertPt;
          }

          //
          // Store the zero value into the allocated memory.
          //
          new StoreInst (Init, AllocInst, iptI);

          //
          // Update statistics.
          //
          ++InitedAllocas;
        }
      }
    }
    return modified;
  }

namespace {
  RegisterPass<InitAllocas> Z("initallocas",
                              "Initialize stack allocations with pointers");
} // end of namespace

NAMESPACE_SC_END


