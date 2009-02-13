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

#include "safecode/SAFECode.h"

#include <iostream>
#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "safecode/VectorListHelper.h"

NAMESPACE_SC_BEGIN

char PreInsertPoolChecks::ID = 0;

static llvm::RegisterPass<PreInsertPoolChecks> pipcPass ("presafecode", "prepare for SAFECode");

// Pass Statistics
namespace {
  // Object registration statistics
  STATISTIC (SavedGlobals,        "Global object registrations avoided");
}

////////////////////////////////////////////////////////////////////////////
// Static Functions
////////////////////////////////////////////////////////////////////////////

//
// Function: insertInitCalls()
//
// Description:
//  Insert the necessary code into the program to initialize the run-time.
//
// Inputs:
//  M              - The module for the program in which to insert
//                   initialization calls.
//
//  DanglingChecks - Flags whether the SAFECode run-time should perform
//                   dangling pointer checks.
//
//  RewriteOOB     - Flags whether the SAFECode run-time should perform
//                   Out-of-Bounds (OOB) pointer rewriting.
//
//  Terminate      - Flags whether the SAFECode run-time should terminate after
//                   the first error it catches.
//
void
PreInsertPoolChecks::insertInitCalls (Module & M,
                                      bool DanglingChecks,
                                      bool RewriteOOB,
                                      bool Terminate) {
  // The pointer to the run-time initialization function
  Constant *RuntimeInit;

  //
  // Create a new function with zero arguments.  This will be the SAFECode
  // run-time constructor; it will be called by static global variable
  // constructor magic before main() is called.
  //
  const char * runtimeCtorName = "_GLOBAL__I__sc_init_runtime";
  intrinsic->addIntrinsic(InsertSCIntrinsic::SC_INTRINSIC_POOL_CONTROL, runtimeCtorName, FunctionType::get(Type::VoidTy, std::vector<const Type*>(), false));

  Function * RuntimeCtor = intrinsic->getIntrinsic(runtimeCtorName).F;
  // Make the runtime constructor compatible with other ctors
  RuntimeCtor->setDoesNotThrow();
  RuntimeCtor->setLinkage(GlobalValue::InternalLinkage);

  //
  // Add a call in the new constructor function to the SAFECode initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create ("entry", RuntimeCtor);
  RuntimeInit = M.getOrInsertFunction ("pool_init_runtime",
                                       Type::VoidTy, 
                                       Type::Int32Ty,
                                       Type::Int32Ty,
                                       Type::Int32Ty,
                                       NULL); 

  std::vector<Value *> args;
  args.push_back (ConstantInt::get(Type::Int32Ty, DanglingChecks));
  args.push_back (ConstantInt::get(Type::Int32Ty, RewriteOOB));
  args.push_back (ConstantInt::get(Type::Int32Ty, Terminate));
  CallInst::Create (RuntimeInit, args.begin(), args.end(), "", BB); 

  //
  // Add a return instruction at the end of the basic block.
  //
  ReturnInst::Create (BB);

  //
  // Insert the run-time ctor into the ctor list.
  //
  std::vector<Constant *> CtorInits;
  CtorInits.push_back (ConstantInt::get (Type::Int32Ty, 65535));
  CtorInits.push_back (RuntimeCtor);
  Constant * RuntimeCtorInit = ConstantStruct::get (CtorInits);

  //
  // Get the current set of static global constructors and add the new ctor
  // to the end of the list (the list seems to be initialized in reverse
  // order).
  //
  std::vector<Constant *> CurrentCtors;
  GlobalVariable * GVCtor = M.getNamedGlobal ("llvm.global_ctors");
  if (GVCtor) {
    if (Constant * C = GVCtor->getInitializer()) {
      for (unsigned index = 0; index < C->getNumOperands(); ++index) {
        CurrentCtors.push_back (C->getOperand (index));
      }
    }

    //
    // Rename the global variable so that we can name our global
    // llvm.global_ctors.
    //
    GVCtor->setName ("removed");
  }
  CurrentCtors.push_back (RuntimeCtorInit);

  //
  // Create a new initializer.
  //
  Constant * NewInit=ConstantArray::get (ArrayType::get (RuntimeCtorInit->
                                                         getType(),
                                                         CurrentCtors.size()),
                                         CurrentCtors);

  //
  // Create the new llvm.global_ctors global variable and replace all uses of
  // the old global variable with the new one.
  //
  new GlobalVariable (NewInit->getType(),
                      false,
                      GlobalValue::AppendingLinkage,
                      NewInit,
                      "llvm.global_ctors",
                      &M);
  return;
}

////////////////////////////////////////////////////////////////////////////
// PreInsertPoolChecks Methods
////////////////////////////////////////////////////////////////////////////

bool 
PreInsertPoolChecks::runOnModule(Module & M) {
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  //
  // Insert code to initialize the SAFECode runtime.
  //
  insertInitCalls (M, DanglingChecks, RewriteOOB, Terminate);

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

  // Create the void pointer type
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 

  //
  // First register argc and argv
  //
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
      Instruction *GVCasted = CastInst::CreatePointerCast(Argv,
					   VoidPtrType, Argv->getName()+"casted",InsertPt);
      const Type* csiType = Type::Int32Ty;
      Value *AllocSize = CastInst::CreateZExtOrBitCast(Argc,
				      csiType, Argc->getName()+"casted",InsertPt);
      AllocSize = BinaryOperator::Create(Instruction::Mul, AllocSize,
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

#if 1
    //
    // FIXME:
    //  This is a hack around what appears to be a DSA bug.  These pointers
    //  should be marked incomplete, but for some reason, in at least one test
    //  case, they are not.
    //
    // Register all of the argv strings
    //
    Constant * RegisterArgv = M.getOrInsertFunction ("poolargvregister",
                                                     Type::VoidTy, 
                                                     Type::Int32Ty,
                                                     Argv->getType(),
                                                     NULL); 
    std::vector<Value *> fargs;
    fargs.push_back (Argc);
    fargs.push_back (Argv);
    CallInst::Create (RegisterArgv, fargs.begin(), fargs.end(), "", InsertPt);
#endif
  }

  // Now iterate over globals and register all the arrays
  Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);

  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GI)) {
      //
      // Skip over several types of globals, including:
      //  llvm.used
      //  llvm.noinline
      //  llvm.global_ctors
      //  Any global pool descriptor
      //  Any global in the meta-data seciton
      //
      // The llvm.global_ctors requires special note.  Apparently, it will not
      // be code generated as the list of constructors if it has any uses
      // within the program.  This transform must ensure, then, that it is
      // never used, even if such a use would otherwise be innocuous.
      //
      std::string name = GV->getName();
      if ((name == "llvm.used")     ||
          (name == "llvm.noinline") ||
          (name == "llvm.global_ctors")) continue;
      if ((GV->getSection()) == "llvm.metadata") continue;
      if (GV->getType() != PoolDescPtrTy) {
        DSGraph *G = paPass->getGlobalsGraph();
        DSNode *DSN  = G->getNodeForValue(GV).getNode();

        // Skip it if there is never a run-time check
        if (dsnPass->isDSNodeChecked(DSN)) {
          ++SavedGlobals;
          continue;
        }

        Value * AllocSize;
        const Type* csiType = Type::Int32Ty;
        const Type * GlobalType = GV->getType()->getElementType();
        AllocSize = ConstantInt::get (csiType, TD->getTypePaddedSize(GlobalType));
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
          Instruction *GVCasted = CastInst::CreatePointerCast(GV,
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

}
#endif

NAMESPACE_SC_END
