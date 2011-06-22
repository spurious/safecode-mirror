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
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Metadata.h"
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
  // Create default debugging values in case we don't find any debug
  // information.  The filename becomes the function name (if the function
  // has a name) and the line number becomes a unique identifier.
  //
  std::string filename = "<unknown>";
  unsigned int lineno  = ++count;
  if (CI->getParent()->getParent()->hasName())
    filename = CI->getParent()->getParent()->getName();

  //
  // Get the line number and source file information for the call if it exists.
  //
  if (MDNode *Dbg = CI->getMetadata(dbgKind)) {
    DILocation Loc (Dbg);
    filename = Loc.getDirectory().str() + Loc.getFilename().str();
    lineno   = Loc.getLineNumber();
    ++FoundSrcInfo;
  }

  //
  // Convert the source filename and line number information into LLVM values.
  //
  Value * LineNumber = ConstantInt::get (Int32Type, lineno);
  Value * SourceFile;
  if (SourceFileMap.find (filename) != SourceFileMap.end()) {
    SourceFile = SourceFileMap[filename];
  } else {
    Constant * FInit = ConstantArray::get (CI->getContext(), filename);
    Module * M = CI->getParent()->getParent()->getParent();
    SourceFile = new GlobalVariable (*M,
                                     FInit->getType(),
                                     true,
                                     GlobalValue::InternalLinkage,
                                     FInit,
                                     "sourcefile");
    SourceFileMap[filename] = SourceFile;
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
  std::string filename = "<unknown>";
  Module * M = CI->getParent()->getParent()->getParent();

  //
  // Get the value for which we want debug information.
  //
  Value * V = CI->getOperand(2)->stripPointerCasts();
  
  //
  // Try to get information about where in the program the value was allocated.
  //
  if (GlobalVariable * GV = dyn_cast<GlobalVariable>(V)) {
    NamedMDNode *NMD = M->getNamedMetadata("llvm.dbg.gv");
    if (NMD) {
      for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
        DIDescriptor DIG(cast<MDNode>(NMD->getOperand(i)));
        if (!DIG.isGlobalVariable())
          continue;
        if (DIGlobalVariable(NMD->getOperand(i)).getGlobal() == GV) {
          DIGlobalVariable Var(NMD->getOperand(i));
          LineNumber = ConstantInt::get (Int32Type, Var.getLineNumber());
          filename = Var.getCompileUnit().getDirectory().str() + Var.getCompileUnit().getFilename().str();
        }
      }
    }
  } else {
    if (Instruction *I = dyn_cast<Instruction>(V))
      if (MDNode *Dbg = I->getMetadata(dbgKind)) {
        DILocation Loc (Dbg);
        filename = Loc.getDirectory().str() + Loc.getFilename().str();
        LineNumber = ConstantInt::get (Int32Type, Loc.getLineNumber());
      }
  }
  if (SourceFileMap.find (filename) != SourceFileMap.end()) {
    SourceFile = SourceFileMap[filename];
  } else {
    Constant * FInit = ConstantArray::get (CI->getContext(), filename);
    Module * M = CI->getParent()->getParent()->getParent();
    SourceFile = new GlobalVariable (*M,
                                     FInit->getType(),
                                     true,
                                     GlobalValue::InternalLinkage,
                                     FInit,
                                     "sourcefile");
    SourceFileMap[filename] = SourceFile;
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

  //
  // Check to see if the debug version of the function already exists.
  //
  bool hadToCreateFunction = true;
  if (F->getParent()->getFunction(F->getName().str() + "_debug"))
    hadToCreateFunction = false;

  FunctionType * DebugFuncType = FunctionType::get (FuncType->getReturnType(),
                                                    ParamTypes,
                                                    false);
  std::string funcdebugname = F->getName().str() + "_debug";
  Constant * FDebug = F->getParent()->getOrInsertFunction (funcdebugname,
                                                           DebugFuncType);

  //
  // Give the function a body.  This is used for ensuring that SAFECode plays
  // nicely with LLVM's bugpoint tool.  By having a body, the program will link
  // correctly even when the intrinsic renaming pass is removed by bugpoint.
  //
  if (hadToCreateFunction) {
    Function * DebugFunc = dyn_cast<Function>(FDebug);
    assert (DebugFunc);

    LLVMContext & Context = F->getContext();
    BasicBlock * entryBB=BasicBlock::Create (Context, "entry", DebugFunc);
    const Type * VoidTy = Type::getVoidTy(Context);
    if (DebugFunc->getReturnType() == VoidTy) {
      ReturnInst::Create (Context, entryBB);
    } else {
      Value * retValue = UndefValue::get (DebugFunc->getReturnType());
      ReturnInst::Create (Context, retValue, entryBB);
    }
  }

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
  // Create the void pointer type
  VoidPtrTy = getVoidPtrType(M);

  //
  // Create needed LLVM types.
  //
  VoidType  = Type::getVoidTy(M.getContext());
  Int8Type  = IntegerType::getInt8Ty(M.getContext());
  Int32Type = IntegerType::getInt32Ty(M.getContext());

  //
  // Get the ID number for debug metadata.
  //
  unsigned dbgKind = M.getContext().getMDKindID("dbg");

  //
  // Transform allocations, load/store checks, and bounds checks.
  //
  LocationSourceInfo LInfo (dbgKind);
  VariableSourceInfo VInfo (dbgKind);

  // FIXME: Technically it should user intrinsic everywhere..
  transformFunction (M.getFunction ("poolalloc"), LInfo);
  transformFunction (M.getFunction ("poolcalloc"), LInfo);
  transformFunction (M.getFunction ("poolrealloc"), LInfo);
  transformFunction (M.getFunction ("poolstrdup"), LInfo);
  transformFunction (M.getFunction ("poolfree"), LInfo);
  transformFunction (M.getFunction ("sc.lscheck"), VInfo);
  transformFunction (M.getFunction ("sc.lscheckui"), VInfo);
  transformFunction (M.getFunction ("sc.lscheckalign"), VInfo);
  transformFunction (M.getFunction ("sc.lscheckalignui"), VInfo);
  transformFunction (M.getFunction ("sc.boundscheck"), VInfo);
  transformFunction (M.getFunction ("sc.boundscheckui"), VInfo);
  transformFunction (M.getFunction ("sc.exactcheck2"), VInfo);
  transformFunction (M.getFunction ("sc.pool_register"), VInfo);
  transformFunction (M.getFunction ("sc.pool_register_stack"), VInfo);
  transformFunction (M.getFunction ("sc.pool_unregister"), VInfo);
  transformFunction (M.getFunction ("sc.pool_unregister_stack"), VInfo);

  // CStdLib
  transformFunction(M.getFunction("pool_strcpy"),  LInfo);
  transformFunction(M.getFunction("pool_strncpy"), LInfo);
  transformFunction(M.getFunction("pool_strlen"),  LInfo);
  transformFunction(M.getFunction("pool_strchr"),  LInfo);
  transformFunction(M.getFunction("pool_strrchr"), LInfo);
  transformFunction(M.getFunction("pool_strncat"), LInfo);
  transformFunction(M.getFunction("pool_strcat"),  LInfo);
  transformFunction(M.getFunction("pool_strstr"),  LInfo);
  transformFunction(M.getFunction("pool_strpbrk"), LInfo);

  transformFunction(M.getFunction("pool_strcmp"),  LInfo);
  transformFunction(M.getFunction("pool_strncmp"), LInfo);
  transformFunction(M.getFunction("pool_memcmp"),  LInfo);
  transformFunction(M.getFunction("pool_strspn"),  LInfo);
  transformFunction(M.getFunction("pool_strcspn"), LInfo);
  transformFunction(M.getFunction("pool_memccpy"), LInfo);
  transformFunction(M.getFunction("pool_memchr"),  LInfo);
  transformFunction(M.getFunction("pool_stpcpy"),  LInfo);
  transformFunction(M.getFunction("pool_bcmp"),    LInfo);
  transformFunction(M.getFunction("pool_bcopy"),   LInfo);
  transformFunction(M.getFunction("pool_index"),   LInfo);
  transformFunction(M.getFunction("pool_rindex"),  LInfo);
  transformFunction(M.getFunction("pool_strcasestr"),  LInfo);
  transformFunction(M.getFunction("pool_strcasecmp"),  LInfo);
  transformFunction(M.getFunction("pool_strncasecmp"), LInfo);

#if 0
  transformFunction(M.getFunction("pool_memcpy"),  LInfo);
  transformFunction(M.getFunction("pool_mempcpy"), LInfo);
  transformFunction(M.getFunction("pool_memmove"), LInfo);
  transformFunction(M.getFunction("pool_memset"),  LInfo);
#endif

  return true;
}

NAMESPACE_SC_END

