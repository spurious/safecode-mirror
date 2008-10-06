//===- PreInsertPoolChecks.cpp - Preparation pass for Check Insertion Pass ---//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass performs several transforms that must be done at global scope for
// inserting SAFECode's run-time checks.
//
//===----------------------------------------------------------------------===//


#define DEBUG_TYPE "pre-insertchecks"

#include <iostream>
#include "safecode/Config/config.h"
#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "VectorListHelper.h"

char llvm::PreInsertPoolChecks::ID = 0;

static llvm::RegisterPass<PreInsertPoolChecks> pipcPass ("presafecode", "prepare for SAFECode");

// Pass Statistics
namespace {
  // Object registration statistics
  STATISTIC (SavedGlobals,        "Global object registrations avoided");
}


namespace llvm {
////////////////////////////////////////////////////////////////////////////
// PreInsertPoolChecks Methods
////////////////////////////////////////////////////////////////////////////

bool 
PreInsertPoolChecks::runOnModule(Module & M) {
  RuntimeInit = M.getOrInsertFunction("pool_init_runtime", Type::VoidTy, 
      Type::Int32Ty, NULL); 
  dsnPass = &getAnalysis<DSNodePass>();
#ifndef LLVA_KERNEL  
  paPass = &getAnalysis<PoolAllocateGroup>();
  assert (paPass && "Pool Allocation Transform *must* be run first!");
  TD = &getAnalysis<TargetData>();
  // Register global arrays and collapsed nodes with global pools
  registerGlobalArraysWithGlobalPools(M);
#endif
  return false;
}

#ifndef LLVA_KERNEL
void
PreInsertPoolChecks::registerGlobalArraysWithGlobalPools(Module &M) {
  //
  // Find the main() function.  For FORTRAN programs converted to C using the
  // NAG f2c tool, the function is named MAIN__.
  //
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    MainFunc = M.getFunction("MAIN__");
    if (MainFunc == 0 || MainFunc->isDeclaration()) {
      std::cerr << "Cannot do array bounds check for this program"
          << "no 'main' function yet!\n";
      abort();
    }
  }

  // First register, argc and argv
  Function::arg_iterator AI = MainFunc->arg_begin(), AE = MainFunc->arg_end();
  if (MainFunc->arg_size() == 2) {
    //There is argc and argv
    Value *Argc = AI;
    AI++;
    Value *Argv = AI;
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*MainFunc);
    Value *PH= dsnPass->getPoolHandle(Argv, MainFunc, *FI);
    Constant *PoolRegister = paPass->PoolRegister;
    BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();

    // Insert the registration after all calls to poolinit().  Also skip
    // cast, alloca, and binary operators.
    while ((isa<CallInst>(InsertPt))  ||
            isa<CastInst>(InsertPt)   ||
            isa<AllocaInst>(InsertPt) ||
            isa<BinaryOperator>(InsertPt)) {
      if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
        if (Function * F = CI->getCalledFunction())
          if (F->getName() == "poolinit")
            ++InsertPt;
          else
            break;
        else
          break;
      else
        ++InsertPt;
    }

    if (PH) {
      Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
      Instruction *GVCasted = CastInst::createPointerCast(Argv,
					   VoidPtrType, Argv->getName()+"casted",InsertPt);
      const Type* csiType = Type::Int32Ty;
      Value *AllocSize = CastInst::createZExtOrBitCast(Argc,
				      csiType, Argc->getName()+"casted",InsertPt);
      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 ConstantInt::get(csiType, 4), "sizetmp", InsertPt);
      std::vector<Value *> args;
      args.push_back (PH);
      args.push_back (GVCasted);
      args.push_back (AllocSize);
      CallInst::Create(PoolRegister, args.begin(), args.end(), "", InsertPt); 
    } else {
      std::cerr << "argv's pool descriptor is not present. \n";
      //	abort();
    }
  }

  // Now iterate over globals and register all the arrays
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
  Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);

  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GI)) {
      // Don't register the llvm.used variable
      if (GV->getName() == "llvm.used") continue;
      if (GV->getType() != PoolDescPtrTy) {
        DSGraph &G = paPass->getGlobalsGraph();
        DSNode *DSN  = G.getNodeForValue(GV).getNode();

#if 0
        // Skip it if there is never a run-time check
        if (dsnPass->CheckedDSNodes.find(DSN) == dsnPass->CheckedDSNodes.end()) {
          ++SavedGlobals;
          continue;
        }
#endif

        Value * AllocSize;
        const Type* csiType = Type::Int32Ty;
        const Type * GlobalType = GV->getType()->getElementType();
        AllocSize = ConstantInt::get (csiType, TD->getABITypeSize(GlobalType));
        Constant *PoolRegister = paPass->PoolRegister;
        BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
        //skip the calls to poolinit
        while ((isa<CallInst>(InsertPt))  ||
                isa<CastInst>(InsertPt)   ||
                isa<AllocaInst>(InsertPt) ||
                isa<BinaryOperator>(InsertPt)) {
          if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
            if (Function * F = CI->getCalledFunction())
              if (F->getName() == "poolinit")
                ++InsertPt;
              else
                break;
            else
              break;
          else
            ++InsertPt;
        }

        Value * PH = paPass->getGlobalPool (DSN);
        if (PH) {
          Instruction *GVCasted = CastInst::createPointerCast(GV,
                 VoidPtrType, GV->getName()+"casted",InsertPt);
          std::vector<Value *> args;
          args.push_back (PH);
          args.push_back (GVCasted);
          args.push_back (AllocSize);
          CallInst::Create(PoolRegister, args.begin(), args.end(), "", InsertPt); 
        } else {
          std::cerr << "pool descriptor not present for " << *GV << std::endl;
#if 0
          abort();
#endif
        }
      }
    }
  }

  //
  // Initialize the runtime.
  //
  BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();

  // Insert the registration after all calls to poolinit().  Also skip
  // cast, alloca, and binary operators.
  while ((isa<CallInst>(InsertPt))  ||
          isa<CastInst>(InsertPt)   ||
          isa<AllocaInst>(InsertPt) ||
          isa<BinaryOperator>(InsertPt)) {
    if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
      if (Function * F = CI->getCalledFunction())
        if (F->getName() == "poolinit")
          ++InsertPt;
        else
          break;
      else
        break;
    else
      ++InsertPt;
  }

  std::vector<Value *> args;
  args.push_back (ConstantInt::get(Type::Int32Ty, DanglingChecks, 0));
  CallInst::Create(RuntimeInit, args.begin(), args.end(), "", InsertPt); 
}
#endif
}
