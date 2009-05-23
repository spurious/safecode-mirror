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
// FIXME:
// During the refactoring, I disable the optimization which don't insert
// registration calls when it founds the DSNode is never checked. It occurs
// everywhere all check insertion passes, it is an optimization and I plan to
// write a separate optimization pass. -- Haohui
//
//===----------------------------------------------------------------------===//


#include "safecode/InsertChecks/RegisterBounds.h"
#include "dsa/DSGraph.h"

using namespace llvm;

NAMESPACE_SC_BEGIN


char RegisterGlobalVariables::ID = 0;
char RegisterMainArgs::ID = 0;
char RegisterCustomizedAllocation::ID = 0;

static llvm::RegisterPass<RegisterGlobalVariables> X1 ("reg-globals", "Register globals into pools");

static llvm::RegisterPass<RegisterMainArgs> X2 ("reg-globals", "Register argv[] into pools");

static llvm::RegisterPass<RegisterCustomizedAllocation> X3 ("reg-custom-alloc", "Register customized allocators");

void
RegisterGlobalVariables::registerGV(GlobalVariable * GV, Instruction * InsertBefore) {
  DSGraph *G = paPass->getGlobalsGraph();
  DSNode *DSN  = G->getNodeForValue(GV).getNode();
    
  const Type* csiType = Type::Int32Ty;
  const Type * GlobalType = GV->getType()->getElementType();
  Value * AllocSize = ConstantInt::get
    (csiType, TD->getTypeAllocSize(GlobalType));
  Value * PH = paPass->getGlobalPool (DSN);
  RegisterVariableIntoPool(PH, GV, AllocSize, InsertBefore);
}

bool
RegisterGlobalVariables::runOnModule(Module & M) {
  init(M);
  paPass = &getAnalysis<PoolAllocateGroup>();
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
    if (strncmp(name.c_str(), "poolalloc.", 10) == 0) continue;
    
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
  Constant * RegisterArgv = M.getOrInsertFunction ("poolargvregister",
                                                   Type::VoidTy, 
                                                   Type::Int32Ty,
                                                   Argv->getType(),
                                                   NULL); 
  std::vector<Value *> fargs;
  fargs.push_back (Argc);
  fargs.push_back (Argv);
  CallInst::Create (RegisterArgv, fargs.begin(), fargs.end(), "", InsertPt);
  return true;
}


///
/// Helper class to abstract the semantics of customized allocators
/// TODO: Move it to DSA
///

class AllocatorInfo {
public:
  virtual ~AllocatorInfo() {}
  virtual Value * getAllocSize(Value * AllocSite) const;
  virtual Value * getFreedPointer(Value * FreeSite) const;
};

///
/// Methods for RegisterCustomizedAllocations
///

bool
RegisterCustomizedAllocation::runOnModule(Module & M) {
  init(M);
  PoolUnregisterFunc = intrinsic->getIntrinsic("sc.pool_unregister").F;
  // FIXME: Write the functionality
  return true;
}

void
RegisterCustomizedAllocation::registerAllocationSite(CallInst * AllocSite, AllocatorInfo * info) {
  Function * F = AllocSite->getParent()->getParent();
  DSNode * Node = dsnPass->getDSNode(AllocSite, F);
  assert (Node && "Allocation site should have a DSNode!");

  //
  // Get the pool handle for the node.
  //
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(AllocSite, F, *FI);
  assert (PH && "Pool handle is missing!");

  BasicBlock::iterator InsertPt = AllocSite;
  ++InsertPt;

  RegisterVariableIntoPool(PH, AllocSite, info->getAllocSize(AllocSite), InsertPt);
}

void
RegisterCustomizedAllocation::registerFreeSite(CallInst * FreeSite, AllocatorInfo * info) {
  Function * F = FreeSite->getParent()->getParent();
  DSNode * Node = dsnPass->getDSNode(FreeSite, F);
  assert (Node && "Allocation site should have a DSNode!");

  //
  // Get the pool handle for the node.
  //
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(FreeSite, F, *FI);
  assert (PH && "Pool handle is missing!");

  Value * ptr = info->getFreedPointer(FreeSite);
  Instruction * Casted = 
    CastInst::CreatePointerCast
    (ptr, PointerType::getUnqual(Type::Int8Ty), 
     ptr->getName()+".casted", FreeSite);
  std::vector<Value *> args;
  args.push_back (PH);
  args.push_back (Casted);
  CallInst::Create(PoolUnregisterFunc, 
                   args.begin(), args.end(), "", FreeSite); 
}

Instruction *
RegisterVariables::CreateRegistrationFunction(Function * F) {
/*  F->setDoesNotThrow();
  F->setLinkage(GlobalValue::InternalLinkage);
*/
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


NAMESPACE_SC_END
