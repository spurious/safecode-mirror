//===- DebugInstrumentation.cpp - Modify run-time checks to track debug info -//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass modifies calls to the pool allocator and SAFECode run-times to
// track source level debugging information.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "DebugAdder"

#include "safecode/DebugInstrumentation.h"

#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "SCUtils.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace llvm;

char llvm::DebugInstrument::ID = 0;

// Register the pass
static
RegisterPass<DebugInstrument> X ("debuginstrument",
                                 "Add Debug Data to SAFECode Run-Time Checks");

///////////////////////////////////////////////////////////////////////////
// Command line options
///////////////////////////////////////////////////////////////////////////
#if 0
cl::opt<bool> InjectEasyDPFaults ("inject-easydp", cl::Hidden,
                                  cl::init(false),
                                  cl::desc("Inject Trivial Dangling Pointer Dereferences"));
#endif

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
#if 0
  STATISTIC (DPFaults,   "Number of Dangling Pointer Faults Injected");
#endif
}

///////////////////////////////////////////////////////////////////////////
// Static Functions
///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////
// Class Methods
///////////////////////////////////////////////////////////////////////////

//
// Method: processFunction()
//
// Description:
//  Process each function in the module.
//
// Inputs:
//  F - The function to transform into a debug version.  This *can be NULL.
//
void
DebugInstrument::transformFunction (Function * F) {
  // If the function does not exist within the module, it does not need to
  // be transformed.
  if (!F) return;

  //
  // Create the function prototype for the debug version of the function.  This
  // function will have an identical type to the original *except* that it will
  // have additional debug parameters at the end.
  //
  const FunctionType * FuncType = F->getFunctionType();
  std::vector<const Type *> ParamTypes (FuncType->param_begin(),
                                        FuncType->param_end());
  ParamTypes.push_back (VoidPtrTy);
  ParamTypes.push_back (Type::Int32Ty);
  FunctionType * DebugFuncType = FunctionType::get (FuncType->getReturnType(),
                                                    ParamTypes,
                                                    false);
  std::string funcdebugname = F->getName() + "_debug";
  Constant * FDebug = F->getParent()->getOrInsertFunction (funcdebugname,
                                                           DebugFuncType);

  //
  // Create dummy line number and source file information for now.
  //
  Value * LineNumber = ConstantInt::get (Type::Int32Ty, 42);
  Constant * SourceFileInit = ConstantArray::get (std::string("/filename.cpp"));
  Value * SourceFile = new GlobalVariable (SourceFileInit->getType(),
                                           true,
                                           GlobalValue::InternalLinkage,
                                           SourceFileInit,
                                           "",
                                           F->getParent());

  //
  // Create a set of call instructions that must be modified.
  //
  std::vector<CallInst *> Worklist;
  Function::use_iterator i, e;
  for (i = F->use_begin(), e = F->use_end(); i != e; ++i) {
    if (CallInst * CI = dyn_cast<CallInst>(i)) {
      Worklist.push_back (CI);
    }
  }

  //
  // Process all call instructions in the worklist.
  //
  while (Worklist.size()) {
    CallInst * CI = Worklist.back();
    Worklist.pop_back();
    std::vector<Value *> args (CI->op_begin(), CI->op_end());
    args.erase (args.begin());
    args.push_back (castTo (SourceFile, VoidPtrTy, "", CI));
    args.push_back (LineNumber);
    CallInst * NewCall = CallInst::Create (FDebug,
                                           args.begin(),
                                           args.end(),
                                           CI->getName(),
                                           CI);
    CI->replaceAllUsesWith (NewCall);
    CI->eraseFromParent();
  }

  return;
}

//
// Method: runOnModule()
//
// Description:
//  This is where the pass begin execution.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
bool
DebugInstrument::runOnModule (Module &M) {
  // Create the void pointer type
  VoidPtrTy = PointerType::getUnqual(Type::Int8Ty); 

  // Transform allocations
  transformFunction (M.getFunction ("poolalloc"));
  return true;
}
