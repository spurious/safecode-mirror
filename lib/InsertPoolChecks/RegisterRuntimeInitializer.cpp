//===- RegisterRuntimeInitializer.cpp ---------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Pass to register runtime initialization calls into user-space programs.
//
//===----------------------------------------------------------------------===//

#include "safecode/InsertChecks/RegisterRuntimeInitializer.h"
#include "safecode/SAFECodeConfig.h"

#include "llvm/Constants.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

char RegisterRuntimeInitializer::ID = 0;

static llvm::RegisterPass<RegisterRuntimeInitializer> X1 ("reg-runtime-init", "Register runtime initializer into programs");


bool
RegisterRuntimeInitializer::runOnModule(llvm::Module & M) {
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  constructInitializer(M);
  insertInitializerIntoGlobalCtorList(M);
  return true;
}

void
RegisterRuntimeInitializer::constructInitializer(llvm::Module & M) {
  //
  // Create a new function with zero arguments.  This will be the SAFECode
  // run-time constructor; it will be called by static global variable
  // constructor magic before main() is called.
  //

  Function * RuntimeCtor = intrinsic->getIntrinsic("sc.init_runtime").F;
  Function * RuntimeInit = intrinsic->getIntrinsic("sc.init_pool_runtime").F;
  Function * RegGlobals  = intrinsic->getIntrinsic("sc.register_globals").F;
//  Function * PoolInit = M.getFunction("poolalloc.init");

  // Make the runtime constructor compatible with other ctors
  RuntimeCtor->setDoesNotThrow();
  RuntimeCtor->setLinkage(GlobalValue::InternalLinkage);

  //
  // Add a call in the new constructor function to the SAFECode initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create ("entry", RuntimeCtor);
 
  // Delegate the responbilities of initializing pool descriptor to the 
  // SAFECode runtime initializer
//  CallInst::Create (PoolInit, "", BB); 

  std::vector<Value *> args;
  if (SCConfig->DanglingPointerChecks)
    args.push_back (Context->getConstantInt(Type::Int32Ty, 1));
  else
    args.push_back (Context->getConstantInt(Type::Int32Ty, 0));
  args.push_back
    (Context->getConstantInt(Type::Int32Ty, SCConfig->RewriteOOB));
  args.push_back
    (Context->getConstantInt(Type::Int32Ty, SCConfig->TerminateOnErrors));
  CallInst::Create (RuntimeInit, args.begin(), args.end(), "", BB); 

  args.clear();
  CallInst::Create (RegGlobals, args.begin(), args.end(), "", BB);


  //
  // Add a return instruction at the end of the basic block.
  //
  ReturnInst::Create (BB);
}

void
RegisterRuntimeInitializer::insertInitializerIntoGlobalCtorList(Module & M) {
  Function * RuntimeCtor = intrinsic->getIntrinsic("sc.init_runtime").F;

  //
  // Insert the run-time ctor into the ctor list.
  //
  std::vector<Constant *> CtorInits;
  CtorInits.push_back (Context->getConstantInt (Type::Int32Ty, 65535));
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
  new GlobalVariable (M,
                      NewInit->getType(),
                      false,
                      GlobalValue::AppendingLinkage,
                      NewInit,
                      "llvm.global_ctors");
}

NAMESPACE_SC_END
