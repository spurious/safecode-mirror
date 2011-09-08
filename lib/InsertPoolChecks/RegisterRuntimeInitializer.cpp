//===- RegisterRuntimeInitializer.cpp ---------------------------*- C++ -*----//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Pass to register runtime initialization calls into user-space programs.
//
//===----------------------------------------------------------------------===//

#include "llvm/Constants.h"
#include "llvm/LLVMContext.h"
#include "safecode/RegisterRuntimeInitializer.h"
#include "safecode/Utility.h"

using namespace llvm;

namespace llvm {

char RegisterRuntimeInitializer::ID = 0;

static llvm::RegisterPass<RegisterRuntimeInitializer>
X1 ("reg-runtime-init", "Register runtime initializer into programs");

bool
RegisterRuntimeInitializer::runOnModule(llvm::Module & M) {
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
  Type * VoidTy  = Type::getVoidTy (M.getContext());
  Type * Int32Ty = IntegerType::getInt32Ty(M.getContext());
  Function * RuntimeCtor = (Function *) M.getOrInsertFunction("pool_ctor",
                                                              VoidTy,
                                                              NULL);

  Function * RuntimeInit = (Function *) M.getOrInsertFunction("pool_init_runtime", VoidTy, Int32Ty, Int32Ty, Int32Ty, NULL);
  Constant * CF = M.getOrInsertFunction ("sc.register_globals", VoidTy, NULL);
  Function * RegGlobals  = dyn_cast<Function>(CF);

  //
  // Make the global registration function internal.
  //
  RegGlobals->setDoesNotThrow();
  RegGlobals->setLinkage(GlobalValue::InternalLinkage);

  // Make the runtime constructor compatible with other ctors
  RuntimeCtor->setDoesNotThrow();
  RuntimeCtor->setLinkage(GlobalValue::InternalLinkage);

  //
  // Empty out the default definition of the SAFECode constructor function.
  // We'll replace it with our own code.
  //
  destroyFunction (RuntimeCtor);

  //
  // Add a call in the new constructor function to the SAFECode initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create (M.getContext(), "entry", RuntimeCtor);
 
  // Delegate the responbilities of initializing pool descriptor to the 
  // SAFECode runtime initializer
//  CallInst::Create (PoolInit, "", BB); 

  Type * Int32Type = IntegerType::getInt32Ty(M.getContext());
  std::vector<Value *> args;

  //
  // FIXME: For now, assume explicit dangling pointer checks are disabled,
  //        rewrite pointers are enabled, and that we should terminate on
  //        errors.  Some more refactoring will be needed to make all of this
  //        work properly.
  //
#if 0
  if (SCConfig.dpChecks())
    args.push_back (ConstantInt::get(Int32Type, 1));
  else
    args.push_back (ConstantInt::get(Int32Type, 0));
  args.push_back (ConstantInt::get(Int32Type, SCConfig.rewriteOOB()));
  args.push_back (ConstantInt::get(Int32Type, SCConfig.terminateOnErrors()));
#else
  args.push_back (ConstantInt::get(Int32Type, 0));
  args.push_back (ConstantInt::get(Int32Type, 1));
  args.push_back (ConstantInt::get(Int32Type, 1));
#endif
  CallInst::Create (RuntimeInit, args, "", BB); 

  args.clear();
  CallInst::Create (RegGlobals, args, "", BB);


  //
  // Add a return instruction at the end of the basic block.
  //
  ReturnInst::Create (M.getContext(), BB);
}

void
RegisterRuntimeInitializer::insertInitializerIntoGlobalCtorList(Module & M) {
  Function * RuntimeCtor = M.getFunction ("pool_ctor");

  //
  // Insert the run-time ctor into the ctor list.
  //
  Type * Int32Type = IntegerType::getInt32Ty(M.getContext());
  std::vector<Constant *> CtorInits;
  CtorInits.push_back (ConstantInt::get (Int32Type, 0));
  CtorInits.push_back (RuntimeCtor);
  StructType * ST = ConstantStruct::getTypeForElements (CtorInits, false);
  Constant * RuntimeCtorInit = ConstantStruct::get (ST, CtorInits);

  //
  // Get the current set of static global constructors and add the new ctor
  // to the list.
  //
  std::vector<Constant *> CurrentCtors;
  GlobalVariable * GVCtor = M.getNamedGlobal ("llvm.global_ctors");
  if (GVCtor) {
    if (Constant * C = GVCtor->getInitializer()) {
      for (unsigned index = 0; index < C->getNumOperands(); ++index) {
        CurrentCtors.push_back (dyn_cast<Constant>(C->getOperand (index)));
      }
    }
  }

  //
  // The ctor list seems to be initialized in different orders on different
  // platforms, and the priority settings don't seem to work.  Examine the
  // module's platform string and take a best guess to the order.
  //
  if (M.getTargetTriple().find ("linux") == std::string::npos)
    CurrentCtors.insert (CurrentCtors.begin(), RuntimeCtorInit);
  else
    CurrentCtors.push_back (RuntimeCtorInit);

  assert (CurrentCtors.back()->getType() == RuntimeCtorInit->getType());

  //
  // Create a new initializer.
  //
  ArrayType * AT = ArrayType::get (RuntimeCtorInit-> getType(),
                                   CurrentCtors.size());
  Constant * NewInit=ConstantArray::get (AT, CurrentCtors);

  //
  // Create the new llvm.global_ctors global variable and replace all uses of
  // the old global variable with the new one.
  //
  Value * newGVCtor = new GlobalVariable (M,
                                          NewInit->getType(),
                                          false,
                                          GlobalValue::AppendingLinkage,
                                          NewInit,
                                          "llvm.global_ctors");
  if (GVCtor)
    newGVCtor->takeName (GVCtor);

  //
  // Delete the old global ctors.
  //
  GVCtor->eraseFromParent ();
  return;
}

}
