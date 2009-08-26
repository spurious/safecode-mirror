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
// Notes:
//  Some of this code is based off of code from the getLocationInfo() method in
//  LLVM.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "debug-instrumentation"

#include "safecode/DebugInstrumentation.h"

#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Constants.h"
#include "llvm/IntrinsicInst.h"
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

NAMESPACE_SC_BEGIN

char DebugInstrument::ID = 0;

// Register the pass
static
RegisterPass<DebugInstrument> X ("debuginstrument",
                                 "Add Debug Data to SAFECode Run-Time Checks");

static int tagCounter = 0;

//
// Basic LLVM Types
//
static const Type * VoidType  = 0;
static const Type * Int8Type  = 0;
static const Type * Int32Type = 0;

///////////////////////////////////////////////////////////////////////////
// Command line options
///////////////////////////////////////////////////////////////////////////

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
  STATISTIC (FoundSrcInfo,   "Number of Source Information Locations Found");
  STATISTIC (QueriedSrcInfo, "Number of Source Information Locations Queried");
}

///////////////////////////////////////////////////////////////////////////
// Static Functions
///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////
// Class Methods
///////////////////////////////////////////////////////////////////////////

GetSourceInfo::~GetSourceInfo() {}

//
// Method: operator()
//
// Description:
//  Return the source information associated with the call instruction by
//  finding the location within the source code in which the call is made.
//
// Inputs:
//  CI - The call instruction
//
// Return value:
//  A pair of LLVM values.  The first is the source file name; the second is
//  the line number.  Default values are given if no source line information
//  can be found.
//
std::pair<Value *, Value *>
LocationSourceInfo::operator() (CallInst * CI) {
  static int count=0;

  //
  // Update the number of source locations queried.
  //
  ++QueriedSrcInfo;

  //
  // Get the line number and source file information for the call.
  //
  const DbgStopPointInst * StopPt = findStopPoint (CI);
  Value * LineNumber;
  Value * SourceFile;
  if (StopPt) {
    LineNumber = StopPt->getLineValue();
    SourceFile = StopPt->getFileName();
    ++FoundSrcInfo;
  } else {
    std::string filename = "<unknown>";
    if (CI->getParent()->getParent()->hasName())
      filename = CI->getParent()->getParent()->getName();
    LineNumber = ConstantInt::get (Int32Type, ++count);
    Constant * FInit = ConstantArray::get (getGlobalContext(), filename);
    Module * M = CI->getParent()->getParent()->getParent();
    SourceFile = new GlobalVariable (*M,
                                     FInit->getType(),
                                     true,
                                     GlobalValue::InternalLinkage,
                                     FInit,
                                     "sourcefile");
  }

  return std::make_pair (SourceFile, LineNumber);
}

//
// Method: operator()
//
// Description:
//  Return the source information associated with a value within the call
//  instruction.  This is mainly intended to provide better source file
//  information to poolregister() calls.
//
// Inputs:
//  CI - The call instruction
//
// Return value:
//  A pair of LLVM values.  The first is the source file name; the second is
//  the line number.  Default values are given if no source line information
//  can be found.
//
std::pair<Value *, Value *>
VariableSourceInfo::operator() (CallInst * CI) {
  assert (((CI->getNumOperands()) > 2) &&
          "Not enough information to get debug info!\n");

  Value * LineNumber;
  Value * SourceFile;

  //
  // Create a default line number and source file information for the call.
  //
  LineNumber = ConstantInt::get (Int32Type, 0);
  Constant * FInit = ConstantArray::get (getGlobalContext(), "<unknown>");
  Module * M = CI->getParent()->getParent()->getParent();
  SourceFile = new GlobalVariable (*M,
                                   FInit->getType(),
                                   true,
                                   GlobalValue::InternalLinkage,
                                   FInit,
                                   "srcfile");

  //
  // Get the value for which we want debug information.
  //
  Value * V = CI->getOperand(2)->stripPointerCasts();

  //
  // Try to get information about where in the program the value was allocated.
  //
  std::string filename;
  if (GlobalVariable * GV = dyn_cast<GlobalVariable>(V)) {
    if (Value * GVDesc = findDbgGlobalDeclare (GV)) {
      DIGlobalVariable Var(cast<GlobalVariable>(GVDesc));
      //Var.getDisplayName(DisplayName);
      LineNumber = ConstantInt::get (Int32Type, Var.getLineNumber());
      Var.getCompileUnit().getFilename(filename);
      Constant * FInit = ConstantArray::get (getGlobalContext(), filename);
      SourceFile = new GlobalVariable (*M,
                                       FInit->getType(),
                                       true,
                                       GlobalValue::InternalLinkage,
                                       FInit,
                                       "srcfile");
    }
  } else {
    if (const DbgDeclareInst *DDI = findDbgDeclare(V)) {
      DIVariable Var (cast<GlobalVariable>(DDI->getVariable()));
      LineNumber = ConstantInt::get (Int32Type, Var.getLineNumber());
      Var.getCompileUnit().getFilename(filename);
      Constant * FInit = ConstantArray::get (getGlobalContext(), filename);
      SourceFile = new GlobalVariable (*M,
                                       FInit->getType(),
                                       true,
                                       GlobalValue::InternalLinkage,
                                       FInit,
                                       "srcfile");
    }
  }

  return std::make_pair (SourceFile, LineNumber);
}


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
DebugInstrument::transformFunction (Function * F, GetSourceInfo & SI) {
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
  ParamTypes.push_back (Int32Type);
  ParamTypes.push_back (VoidPtrTy);
  ParamTypes.push_back (Int32Type);

  FunctionType * DebugFuncType = FunctionType::get (FuncType->getReturnType(),
                                                    ParamTypes,
                                                    false);
  std::string funcdebugname = F->getName().str() + "_debug";
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
    Value * LineNumber;
    Value * SourceFile;
    std::pair<Value *, Value *> Info = SI (CI);
    SourceFile = Info.first;
    LineNumber = Info.second;

    //
    // If the source filename is in the meta-data section, make a copy of it in
    // the default section.  This ensures that it gets code generated.
    //
    if (ConstantExpr * GEP = dyn_cast<ConstantExpr>(SourceFile)) {
      if (GlobalVariable * GV = dyn_cast<GlobalVariable>(GEP->getOperand(0))) {
        if (GV->hasSection()) {
          GlobalVariable * SrcGV = new GlobalVariable (*(F->getParent()),
                                                       GV->getType()->getElementType(),
                                                       GV->isConstant(),
                                                       GV->getLinkage(),
                                                       GV->getInitializer(),
                                                       GV->getName(),
                                                       0,
                                                       GV->isThreadLocal(),
                                                       0);
          SrcGV->copyAttributesFrom (GV);
          SrcGV->setSection ("");
          SourceFile = SrcGV;
        }
      }
    }

    //
    // Transform the function call.
    //
    std::vector<Value *> args (CI->op_begin(), CI->op_end());
    args.erase (args.begin());
    args.push_back (ConstantInt::get(Int32Type, tagCounter++));

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
  InsertSCIntrinsic & intrinsic = getAnalysis<InsertSCIntrinsic>();

  // Create the void pointer type
  VoidPtrTy = getVoidPtrType();

  //
  // Create needed LLVM types.
  //
  VoidType  = Type::getVoidTy(getGlobalContext());
  Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
  Int32Type = IntegerType::getInt32Ty(getGlobalContext());

  //
  // Transform allocations, load/store checks, and bounds checks.
  //
  LocationSourceInfo LInfo;
  VariableSourceInfo VInfo;
  // FIXME: Technically it should user intrinsic everywhere..
  transformFunction (M.getFunction ("poolalloc"), LInfo);
  transformFunction (M.getFunction ("poolcalloc"), LInfo);
  transformFunction (M.getFunction ("poolfree"), LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.lscheck").F, LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.lscheckalign").F, LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.boundscheck").F, LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.boundscheckui").F, LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.exactcheck2").F, LInfo);
  transformFunction (intrinsic.getIntrinsic("sc.pool_register").F, LInfo);
  return true;
}

NAMESPACE_SC_END

