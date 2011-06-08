//===- RegisterBounds.cpp ---------------------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Various passes to register the bound information of variables into the pools
//
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sc-register"

#include "safecode/InsertChecks/RegisterBounds.h"
#include "safecode/Support/AllocatorInfo.h"
#include "dsa/DSGraph.h"
#include "SCUtils.h"

#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"

#include <functional>

using namespace llvm;

namespace {
  // Statistics
  STATISTIC (RegisteredGVs,      "Number of registered global variables");
  STATISTIC (RegisteredByVals,   "Number of registered byval arguments");
  STATISTIC (RegisteredHeapObjs, "Number of registered heap objects");
}

NAMESPACE_SC_BEGIN

char RegisterGlobalVariables::ID = 0;
char RegisterMainArgs::ID = 0;
char RegisterFunctionByvalArguments::ID=0;
char RegisterCustomizedAllocation::ID = 0;


static llvm::RegisterPass<RegisterGlobalVariables>
X1 ("reg-globals", "Register globals into pools", true);

static llvm::RegisterPass<RegisterMainArgs>
X2 ("reg-argv", "Register argv[] into pools", true);

static llvm::RegisterPass<RegisterCustomizedAllocation>
X3 ("reg-custom-alloc", "Register customized allocators", true);

static llvm::RegisterPass<RegisterFunctionByvalArguments>
X4 ("reg-byval-args", "Register byval arguments for functions", true);

//
// Method: registerGV()
//
// Description:
//  This method adds code into a program to register a global variable into its
//  pool.
//
void
RegisterGlobalVariables::registerGV (GlobalVariable * GV,
                                     Instruction * InsertBefore) {
  // Don't bother to register external global variables
  if (GV->isDeclaration()) {
    return;
  } 

  //
  // Get the pool into which the global should be registered.
  //
  Value * PH = ConstantPointerNull::get (getVoidPtrType(GV->getContext()));
  const Type* csiType = IntegerType::getInt32Ty(GV->getContext());
  const Type * GlobalType = GV->getType()->getElementType();
  Value * AllocSize = ConstantInt::get 
    (csiType, TD->getTypeAllocSize(GlobalType));
  RegisterVariableIntoPool(PH, GV, AllocSize, InsertBefore);

  // Update statistics
  ++RegisteredGVs;
}

bool
RegisterGlobalVariables::runOnModule(Module & M) {
  init("sc.pool_register_global");

  //
  // Get required analysis passes.
  //
  TD       = &getAnalysis<TargetData>();

  Instruction * InsertPt = 
    CreateRegistrationFunction(intrinsic->getIntrinsic("sc.register_globals").F);

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
  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    GlobalVariable *GV = dyn_cast<GlobalVariable>(GI);
    if (!GV) continue;
    std::string name = GV->getName();

    // Skip globals in special sections
    if ((GV->getSection()) == "llvm.metadata") continue;

    if (strncmp(name.c_str(), "llvm.", 5) == 0) continue;
    if (strncmp(name.c_str(), "__poolalloc", 11) == 0) continue;
   
    if (SCConfig.svaEnabled()) {
      // Linking fails when registering objects in section exitcall.exit
      if (GV->getSection() == ".exitcall.exit") continue;
    }

    registerGV(GV, InsertPt);    
  }

  return true;
}

bool
RegisterMainArgs::runOnModule(Module & M) {
  init("sc.pool_register");
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    llvm::errs() << "Cannot do array bounds check for this program"
               << "no 'main' function yet!\n";
    abort();
  }

  if (MainFunc->arg_size() != 2) {
    // No argc / argv, return
    return false;
  }

  Function::arg_iterator AI = MainFunc->arg_begin();
  Value *Argc = AI;
  Value *Argv = ++AI;


  Instruction * InsertPt = MainFunc->front().begin(); 

  //
  // FIXME:
  //  This is a hack around what appears to be a DSA bug.  These pointers
  //  should be marked incomplete, but for some reason, in at least one test
  //  case, they are not.
  //
  // Register all of the argv strings
  //
  // FIXME: Should use the intrinsic interface
  Function * RegisterArgv = intrinsic->getIntrinsic("sc.pool_argvregister").F;

  std::vector<Value *> fargs;
  fargs.push_back (Argc);
  fargs.push_back (Argv);
  CallInst::Create (RegisterArgv, fargs.begin(), fargs.end(), "", InsertPt);
  return true;
}


///
/// Methods for RegisterCustomizedAllocations
///

void
RegisterCustomizedAllocation::proceedAllocator(Module * M, AllocatorInfo * info) {
  Function * allocFunc = M->getFunction(info->getAllocCallName());
  if (allocFunc) {
    for (Value::use_iterator it = allocFunc->use_begin(), 
           end = allocFunc->use_end(); it != end; ++it)
      if (CallInst * CI = dyn_cast<CallInst>(*it)) {
        registerAllocationSite(CI, info);
        ++RegisteredHeapObjs;
      }
  }
  
  //
  // Find the deallocation function, visit all uses of it, and process all
  // calls to it.
  //
  Function * freeFunc = M->getFunction(info->getFreeCallName());
  if (freeFunc) {
    for (Value::use_iterator it = freeFunc->use_begin(),
           end = freeFunc->use_end(); it != end; ++it) {
      if (CallInst * CI = dyn_cast<CallInst>(*it)) {
        registerFreeSite(CI, info);
      }

      //
      // If the user is a constant expression, the constant expression may be
      // a cast that is used by a call instruction.  Get the enclosing call
      // instruction if so.
      //
      if (ConstantExpr * CE = dyn_cast<ConstantExpr>(*it)) {
        if (CE->isCast()) {
          for (Value::use_iterator iit = CE->use_begin(),
                 end = CE->use_end(); iit != end; ++iit) {
            if (CallInst * CI = dyn_cast<CallInst>(*iit)) {
              if (CI->getCalledValue() == CE) {
                registerFreeSite(CI, info);
              }
            }
          }
        }
      }
    }
  }
}

void
RegisterCustomizedAllocation::proceedReallocator(Module * M, ReAllocatorInfo * info) {
  Function * allocFunc = M->getFunction(info->getAllocCallName());
  if (allocFunc) {
    for (Value::use_iterator it = allocFunc->use_begin(), 
           end = allocFunc->use_end(); it != end; ++it)
      if (CallInst * CI = dyn_cast<CallInst>(*it)) {
        registerReallocationSite(CI, info);
        ++RegisteredHeapObjs;
      }
  }
  
  Function * freeFunc = M->getFunction(info->getFreeCallName());
  if (freeFunc) {
    for (Value::use_iterator it = freeFunc->use_begin(),
           end = freeFunc->use_end(); it != end; ++it)
      if (CallInst * CI = dyn_cast<CallInst>(*it))
        registerFreeSite(CI, info);
  }
}

bool
RegisterCustomizedAllocation::runOnModule(Module & M) {
  init("sc.pool_register");

  //
  // Get the functions for reregistering and deregistering memory objects.
  //
  const Type * Int32Type = IntegerType::getInt32Ty (M.getContext());
  PoolReregisterFunc = (Function *) M.getOrInsertFunction ("sc.pool_reregister",
                                                           Type::getVoidTy (M.getContext()),
                                                           getVoidPtrType (M),
                                                           getVoidPtrType (M),
                                                           getVoidPtrType (M),
                                                           Int32Type,
                                                           NULL);
  PoolUnregisterFunc = intrinsic->getIntrinsic("sc.pool_unregister").F;
  AllocatorInfoPass & AIP = getAnalysis<AllocatorInfoPass>();
  for (AllocatorInfoPass::alloc_iterator it = AIP.alloc_begin(),
      end = AIP.alloc_end(); it != end; ++it) {
    proceedAllocator(&M, *it);
  }

  for (AllocatorInfoPass::realloc_iterator it = AIP.realloc_begin(),
      end = AIP.realloc_end(); it != end; ++it) {
    proceedReallocator(&M, *it);
  }

  return true;
}

void
RegisterCustomizedAllocation::registerAllocationSite(CallInst * AllocSite, AllocatorInfo * info) {
  //
  // Get the pool handle for the node.
  //
  LLVMContext & Context = AllocSite->getContext();
  Value * PH = ConstantPointerNull::get (getVoidPtrType (Context));

  BasicBlock::iterator InsertPt = AllocSite;
  ++InsertPt;

  Value * AllocSize = info->getOrCreateAllocSize(AllocSite);
  if (!AllocSize->getType()->isIntegerTy(32)) {
    AllocSize = CastInst::CreateIntegerCast (AllocSize,
                                             Type::getInt32Ty(Context),
                                             false,
                                             AllocSize->getName(),
                                             InsertPt);
  }
  RegisterVariableIntoPool (PH, AllocSite, AllocSize, InsertPt);
}

void
RegisterCustomizedAllocation::registerReallocationSite(CallInst * AllocSite, ReAllocatorInfo * info) {
  //
  // Get the pool handle for the node.
  //
  LLVMContext & Context = AllocSite->getContext();
  Value * PH = ConstantPointerNull::get (getVoidPtrType (Context));

  //
  // Find the instruction following the reallocation site; this will be where
  // we insert the reallocation registration call.
  //
  BasicBlock::iterator InsertPt = AllocSite;
  ++InsertPt;

  //
  // Get the size of the allocation and cast it to the desired type.
  //
  Value * AllocSize = info->getOrCreateAllocSize(AllocSite);
  if (!AllocSize->getType()->isIntegerTy(32)) {
    AllocSize = CastInst::CreateIntegerCast (AllocSize,
                                             Type::getInt32Ty(Context),
                                             false,
                                             AllocSize->getName(),
                                             InsertPt);
  }

  //
  // Get the pointers to the old and new memory buffer.
  //
  Value * OldPtr = castTo (info->getAllocedPointer (AllocSite),
                           getVoidPtrType(PH->getContext()),
                           (info->getAllocedPointer (AllocSite))->getName(),
                           InsertPt);
  Value * NewPtr = castTo (AllocSite,
                           getVoidPtrType(PH->getContext()),
                           AllocSite->getName(),
                           InsertPt);

  //
  // Create the call to reregister the allocation.
  //
  std::vector<Value *> args;
  args.push_back (PH);
  args.push_back (NewPtr);
  args.push_back (OldPtr);
  args.push_back (AllocSize);
  CallInst::Create(PoolReregisterFunc, args.begin(), args.end(), "", InsertPt); 
  return;
}

void
RegisterCustomizedAllocation::registerFreeSite (CallInst * FreeSite,
                                                AllocatorInfo * info) {
  //
  // Get the pointer being deallocated.  Strip away casts as these may have
  // been inserted after the DSA pass was executed and may, therefore, not have
  // a pool handle.
  //
  Value * ptr = info->getFreedPointer(FreeSite)->stripPointerCasts();

  //
  // If the pointer is a constant NULL pointer, then don't bother inserting
  // an unregister call.
  //
  if (isa<ConstantPointerNull>(ptr))
    return;

  //
  // Get the pool handle for the freed pointer.
  //
  LLVMContext & Context = FreeSite->getContext();
  Value * PH = ConstantPointerNull::get (getVoidPtrType(Context));

  //
  // Cast the pointer being unregistered and the pool handle into void pointer
  // types.
  //
  Value * Casted = castTo (ptr,
                           getVoidPtrType(Context),
                           ptr->getName()+".casted",
                           FreeSite);

  Value * PHCasted = castTo (PH,
                             getVoidPtrType(Context), 
                             PH->getName()+".casted",
                             FreeSite);

  //
  // Create a call that will unregister the object.
  //
  std::vector<Value *> args;
  args.push_back (PHCasted);
  args.push_back (Casted);
  CallInst::Create (PoolUnregisterFunc, args.begin(), args.end(), "", FreeSite);
}

Instruction *
RegisterVariables::CreateRegistrationFunction(Function * F) {
  //
  // Destroy any code that currently exists in the function.  We are going to
  // replace it.
  //
  destroyFunction (F);

  //
  // Add a call in the new constructor function to the SAFECode initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create (getGlobalContext(), "entry", F);

  //
  // Add a return instruction at the end of the basic block.
  //
  return ReturnInst::Create (getGlobalContext(), BB);
}

RegisterVariables::~RegisterVariables() {}

//
// Method: init()
//
// Description:
//  This method performs some initialization that is common to all subclasses
//  of this pass.
//
// Inputs:
//  registerName - The name of the function with which to register object.
//
void
RegisterVariables::init(std::string registerName) {
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  PoolRegisterFunc = intrinsic->getIntrinsic(registerName).F;  
}


void
RegisterVariables::RegisterVariableIntoPool(Value * PH, Value * val, Value * AllocSize, Instruction * InsertBefore) {
  if (!PH) {
    llvm::errs() << "pool descriptor not present for " << val->getNameStr()
                 << "\n";
    return;
  }
  Instruction *GVCasted = 
    CastInst::CreatePointerCast
    (val, getVoidPtrType(PH->getContext()), 
     val->getName()+".casted", InsertBefore);
  Instruction * PHCasted = 
    CastInst::CreatePointerCast
    (PH, getVoidPtrType(PH->getContext()), 
     PH->getName()+".casted", InsertBefore);
  std::vector<Value *> args;
  args.push_back (PHCasted);
  args.push_back (GVCasted);
  args.push_back (AllocSize);
  CallInst::Create(PoolRegisterFunc, 
                   args.begin(), args.end(), "", InsertBefore); 
}

bool
RegisterFunctionByvalArguments::runOnModule(Module & M) {
  init("sc.pool_register_stack");

  //
  // Fetch prerequisite analysis passes.
  //
  TD        = &getAnalysis<TargetData>();
  intrinsic = &getAnalysis<InsertSCIntrinsic>();

  //
  // Insert required intrinsics.
  //
  StackFree = intrinsic->getIntrinsic("sc.pool_unregister_stack").F;  

  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++ I) {
    //
    // Don't process declarations.
    //
    if (I->isDeclaration()) continue;

    //
    // Check the name of the function to see if it is a run-time function that
    // we should not process.
    //
    if (I->hasName()) {
      std::string Name = I->getName();
      if ((Name.find ("__poolalloc") == 0) || (Name.find ("sc.") == 0))
        continue;
    }

    runOnFunction(*I);
  }
  return true;
}

//
// Method: runOnFunction()
//
// Description:
//  Entry point for this function pass.  This method will insert calls to
//  register the memory allocated for the byval arguments passed into the
//  specified function.
//
// Return value:
//  true  - The function was modified.
//  false - The function was not modified.
//
bool
RegisterFunctionByvalArguments::runOnFunction (Function & F) {
  //
  // Scan through all arguments of the function.  For each byval argument,
  // insert code to register the argument into its repspective pool.  Also
  // record the mapping between argument and pool so that we can insert
  // deregistration code at function exit.
  //
  typedef SmallVector<std::pair<Value*, Argument *>, 4> RegisteredArgTy;
  RegisteredArgTy registeredArguments;
  LLVMContext & Context = F.getContext();
  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    if (I->hasByValAttr()) {
      assert (isa<PointerType>(I->getType()));
      const PointerType * PT = cast<PointerType>(I->getType());
      const Type * ET = PT->getElementType();
      Value * AllocSize = ConstantInt::get
        (IntegerType::getInt32Ty(Context), TD->getTypeAllocSize(ET));
      Value * PH = ConstantPointerNull::get (getVoidPtrType(Context));
      Instruction * InsertBefore = &(F.getEntryBlock().front());
      RegisterVariableIntoPool(PH, &*I, AllocSize, InsertBefore);
      registeredArguments.push_back(std::make_pair<Value*, Argument*>(PH, &*I));
    }
  }

  //
  // Find all basic blocks which terminate the function.
  //
  SmallSet<BasicBlock *, 4> exitBlocks;
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (isa<ReturnInst>(*I) || isa<UnwindInst>(*I)) {
      exitBlocks.insert(I->getParent());
    }
  }

  //
  // At each function exit, insert code to deregister all byval arguments.
  //
  for (SmallSet<BasicBlock*, 4>::const_iterator BI = exitBlocks.begin(),
                                                BE = exitBlocks.end();
       BI != BE; ++BI) {
    for (RegisteredArgTy::const_iterator I = registeredArguments.begin(),
                                         E = registeredArguments.end();
         I != E; ++I) {
      SmallVector<Value *, 2> args;
      Instruction * Pt = &((*BI)->back());
      Value *CastPH  = castTo (I->first, getVoidPtrType(Context), Pt);
      Value *CastV = castTo (I->second, getVoidPtrType(Context), Pt);
      args.push_back (CastPH);
      args.push_back (CastV);
      CallInst::Create (StackFree, args.begin(), args.end(), "", Pt);
    }
  }

  //
  // Update the statistics on the number of registered byval arguments.
  //  
  if (registeredArguments.size())
    RegisteredByVals += registeredArguments.size();

  return true;
}

NAMESPACE_SC_END
