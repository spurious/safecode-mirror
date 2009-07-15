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


#include "safecode/InsertChecks/RegisterBounds.h"
#include "safecode/Support/AllocatorInfo.h"
#include "dsa/DSGraph.h"
#include "SCUtils.h"

#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/SmallSet.h"

#if 0
#include <tr1/functional>
#endif

#include <functional>

using namespace llvm;

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

void
RegisterGlobalVariables::registerGV(GlobalVariable * GV, Instruction * InsertBefore) {
  if (GV->isDeclaration()) {
    // Don't bother to register external global variables
    return;
  } 

  DSNode *DSN  = dsnPass->getDSNodeForGlobalVariable(GV);
  if (DSN) {  
    const Type* csiType = Type::Int32Ty;
    const Type * GlobalType = GV->getType()->getElementType();
    Value * AllocSize = Context->getConstantInt
      (csiType, TD->getTypeAllocSize(GlobalType));
    Value * PH = dsnPass->paPass->getGlobalPool (DSN);
    RegisterVariableIntoPool(PH, GV, AllocSize, InsertBefore);
  } else {
    llvm::cerr << "Warning: Missing DSNode for global" << *GV << "\n";
  }
}

bool
RegisterGlobalVariables::runOnModule(Module & M) {
  init(M);
  dsnPass = &getAnalysis<DSNodePass>();
  TD = &getAnalysis<TargetData>();

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
    if ((GV->getSection()) == "llvm.metadata") continue;

    if (strncmp(name.c_str(), "llvm.", 5) == 0) continue;
    if (strncmp(name.c_str(), "__poolalloc", 11) == 0) continue;
   
    if (SCConfig->SVAEnabled) {
      // Linking fails when registering objects in section exitcall.exit
      if (GV->getSection() == ".exitcall.exit") continue;
    }

    registerGV(GV, InsertPt);    
  }

  return true;
}

bool
RegisterMainArgs::runOnModule(Module & M) {
  init(M);
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    llvm::cerr << "Cannot do array bounds check for this program"
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
      if (CallInst * CI = dyn_cast<CallInst>(*it))
        registerAllocationSite(CI, info);
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
  init(M);
  dsnPass = &getAnalysis<DSNodePass>();
  paPass = &getAnalysis<PoolAllocateGroup>();

  PoolUnregisterFunc = intrinsic->getIntrinsic("sc.pool_unregister").F;
#if 0
  // Unfortunaly, our machines only have gcc3, which does not support
  // TR1..
  std::for_each
    (SCConfig->alloc_begin(), SCConfig->alloc_end(),
     std::tr1::bind
     (&RegisterCustomizedAllocation::proceedAllocator,
      this, &M, std::tr1::placeholders::_1));
#else
  for (SAFECodeConfiguration::alloc_iterator it = SCConfig->alloc_begin(),
      end = SCConfig->alloc_end(); it != end; ++it) {
    proceedAllocator(&M, *it);
  }

#endif

  return true;
}

void
RegisterCustomizedAllocation::registerAllocationSite(CallInst * AllocSite, AllocatorInfo * info) {
  Function * F = AllocSite->getParent()->getParent();

  //
  // Get the pool handle for the node.
  //
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(AllocSite, F, *FI);
  assert (PH && "Pool handle is missing!");

  BasicBlock::iterator InsertPt = AllocSite;
  ++InsertPt;

  RegisterVariableIntoPool (PH, AllocSite, info->getAllocSize(AllocSite), InsertPt);
}

void
RegisterCustomizedAllocation::registerFreeSite (CallInst * FreeSite,
                                                AllocatorInfo * info) {
  // Function containing the call to the deallocator
  Function * F = FreeSite->getParent()->getParent();

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
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(ptr, F, *FI);
  assert (PH && "Pool handle is missing!");

  //
  // Cast the pointer being unregistered and the pool handle into void pointer
  // types.
  //
  Value * Casted = castTo (ptr,
                           PointerType::getUnqual(Type::Int8Ty),
                           ptr->getName()+".casted",
                           FreeSite);

  Value * PHCasted = castTo (PH,
                             PointerType::getUnqual(Type::Int8Ty), 
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
  // Add a call in the new constructor function to the SAFECode initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create ("entry", F);

  //
  // Add a return instruction at the end of the basic block.
  //
  return ReturnInst::Create (BB);
}

RegisterVariables::~RegisterVariables() {}

void
RegisterVariables::init(Module & M) {
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  PoolRegisterFunc =
    intrinsic->getIntrinsic("sc.pool_register").F;  
}


void
RegisterVariables::RegisterVariableIntoPool(Value * PH, Value * val, Value * AllocSize, Instruction * InsertBefore) {
  if (!PH) {
    llvm::cerr << "pool descriptor not present for " << *val << std::endl;
    return;
  }
  Instruction *GVCasted = 
    CastInst::CreatePointerCast
    (val, PointerType::getUnqual(Type::Int8Ty), 
     val->getName()+".casted", InsertBefore);
  Instruction * PHCasted = 
    CastInst::CreatePointerCast
    (PH, PointerType::getUnqual(Type::Int8Ty), 
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
  init(M);
  dsnPass = &getAnalysis<DSNodePass>();
  TD = &getAnalysis<TargetData>();
  intrinsic = &getAnalysis<InsertSCIntrinsic>();

  StackFree = intrinsic->getIntrinsic("sc.pool_unregister").F;  

  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++ I) {
    const char * name = I->getName().c_str();
    if (I->isDeclaration() || strncmp(name, "__poolalloc", 11) == 0 || strncmp(name, "sc.", 3) == 0)
      continue;

    runOnFunction(*I);
  }
  return true;
}

bool
RegisterFunctionByvalArguments::runOnFunction(Function & F) {
  typedef SmallVector<std::pair<Value*, Argument *>, 4> RegisteredArgTy;
  RegisteredArgTy registeredArguments;
  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    if (I->hasByValAttr()) {
      assert (isa<PointerType>(I->getType()));
      const PointerType * PT = cast<PointerType>(I->getType());
      const Type * ET = PT->getElementType();
      Value * AllocSize = Context->getConstantInt
        (Type::Int32Ty, TD->getTypeAllocSize(ET));
      PA::FuncInfo *FI = dsnPass->paPass->getFuncInfoOrClone(F);
      Value *PH = dsnPass->getPoolHandle(&*I, &F, *FI);
      Instruction * InsertBefore = &(F.getEntryBlock().front());
      RegisterVariableIntoPool(PH, &*I, AllocSize, InsertBefore);
      registeredArguments.push_back(std::make_pair<Value*, Argument*>(PH, &*I));
    }
  }

  SmallSet<BasicBlock *, 4> exitBlocks;
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (isa<ReturnInst>(*I) || isa<UnwindInst>(*I)) {
      exitBlocks.insert(I->getParent());
    }
  }

  for (SmallSet<BasicBlock*, 4>::const_iterator BI = exitBlocks.begin(), BE = exitBlocks.end(); BI != BE; ++BI) {
    for (RegisteredArgTy::const_iterator I = registeredArguments.begin(), E = registeredArguments.end(); I != E; ++I) {
      SmallVector<Value *, 2> args;
      Instruction * Pt = &((*BI)->back());
      Value *CastPH  = castTo (I->first, PointerType::getUnqual(Type::Int8Ty), Pt);
      Value *CastV = castTo (I->second, PointerType::getUnqual(Type::Int8Ty), Pt);
      args.push_back (CastPH);
      args.push_back (CastV);
      CallInst::Create (StackFree, args.begin(), args.end(), "", Pt);
    }
  }

  return true;
}

NAMESPACE_SC_END
