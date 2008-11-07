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

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
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

    //
    // Get the line number and source file information for the call.
    //
    ValueLocation * SourceInfo = DebugLocator.getInstrLocation (CI);
    Value * LineNumber = ConstantInt::get (Type::Int32Ty, SourceInfo->statement.lineNo);
    Value * SourceFile = SourceInfo->statement.filename;
    if (!SourceFile) {
      Constant * FInit = ConstantArray::get ("<unknown>");
      SourceFile = new GlobalVariable (FInit->getType(),
                                       true,
                                       GlobalValue::InternalLinkage,
                                       FInit,
                                       "sourcefile",
                                       F->getParent());
    }

    //
    // If the source filename is in the meta-data section, move it to the
    // default section.
    //
    if (ConstantExpr * GEP = dyn_cast<ConstantExpr>(SourceFile)) {
      if (GlobalVariable * GV = dyn_cast<GlobalVariable>(GEP->getOperand(0))) {
        GV->setSection ("");
      }
    }

    //
    // Transform the function call.
    //
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

  //
  // Get the debugging information for the current module.
  //
  DebugLocator.setModule (&M);

  //
  // Transform allocations, load/store checks, and bounds checks.
  //
  transformFunction (M.getFunction ("poolalloc"));
  transformFunction (M.getFunction ("poolcheck"));
  transformFunction (M.getFunction ("boundscheckui"));
  transformFunction (M.getFunction ("exactcheck2"));
  return true;
}
