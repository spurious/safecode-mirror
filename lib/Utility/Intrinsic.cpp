//===- Intrinsic.cpp - Insert declaration of SAFECode intrinsics -------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a module pass to insert declarations of the SAFECode
// intrinsics into the bitcode file. It also provides interfaces for later
// passes which use these intrinsics.
//
//===----------------------------------------------------------------------===//

#include "safecode/Intrinsic.h"
#include "safecode/VectorListHelper.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/Support/AllocatorInfo.h"

#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"

#include <set>
#include <queue>

using namespace llvm;

NAMESPACE_SC_BEGIN

void
InsertSCIntrinsic::addDebugIntrinsic(const char * name) {
  const IntrinsicInfoTy & info = getIntrinsic(name);
  const Type * Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
  const Type * Int32Type = IntegerType::getInt32Ty(getGlobalContext());
  static const Type * vpTy = PointerType::getUnqual(Int8Type);

  Function * F = info.F;
  const FunctionType * FuncType = F->getFunctionType();
  std::vector<const Type *> ParamTypes (FuncType->param_begin(),
                                        FuncType->param_end());
  // Tag field
  ParamTypes.push_back (Int32Type);

  ParamTypes.push_back (vpTy);
  ParamTypes.push_back (Int32Type);


  FunctionType * DebugFuncType = FunctionType::get (FuncType->getReturnType(),
                                                    ParamTypes,
                                                    false);

  Twine funcdebugname = F->getName() + "_debug";

  addIntrinsic
    (funcdebugname.str().c_str(),
     info.flag | SC_INTRINSIC_DEBUG_INSTRUMENTATION,
     DebugFuncType,
     info.ptrindex);
}

//
// Method: runOnModule()
//
// Description:
//  This is the entry point for this Module Pass.  It will insert the necessary
//  SAFECode run-time functions into the Module.
//
// Inputs:
//  M - A reference to the Module to modify.
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
InsertSCIntrinsic::runOnModule(Module & M) {
  currentModule = &M;
  TD = &getAnalysis<TargetData>();
  const Type * VoidTy = Type::getVoidTy(getGlobalContext());
  const Type * Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
  const Type * Int32Ty = IntegerType::getInt32Ty(getGlobalContext());
  const Type * vpTy = PointerType::getUnqual(Int8Type);

  FunctionType * LSCheckTy = FunctionType::get
    (VoidTy, args<const Type*>::list(vpTy, vpTy), false);

  FunctionType * LSCheckAlignTy = FunctionType::get
    (VoidTy, args<const Type*>::list(vpTy, vpTy, Int32Ty), false);

  FunctionType * BoundsCheckTy = FunctionType::get
    (vpTy, args<const Type*>::list(vpTy, vpTy, vpTy), false);

  FunctionType * ExactCheck2Ty = FunctionType::get
    (vpTy, args<const Type*>::list(vpTy, vpTy, Int32Ty), false);

  FunctionType * FuncCheckTy = FunctionType::get
    (VoidTy, args<const Type*>::list(Int32Ty, vpTy, vpTy), true);

  FunctionType * GetActualValTy = FunctionType::get
    (vpTy, args<const Type*>::list(vpTy, vpTy), false);

  FunctionType * PoolRegTy = FunctionType::get
    (VoidTy, args<const Type*>::list(vpTy, vpTy, Int32Ty), false);

  FunctionType * PoolUnregTy = FunctionType::get
    (VoidTy, args<const Type*>::list(vpTy, vpTy), false);

  FunctionType * PoolArgRegTy = FunctionType::get
    (vpTy, args<const Type*>::list(Int32Ty, PointerType::getUnqual(vpTy)), false);

  FunctionType * RegisterGlobalsTy = FunctionType::get
    (VoidTy, args<const Type*>::list(), false);

  FunctionType * InitRuntimeTy = RegisterGlobalsTy;

  FunctionType * InitPoolRuntimeTy = FunctionType::get
    (VoidTy, args<const Type*>::list(Int32Ty, Int32Ty, Int32Ty), false);

  // Format string function related intrinsics

  FunctionType * FSParameterTy = FunctionType::get
    (vpTy, args<const Type*>::list(vpTy, vpTy, vpTy, Int8Type), false);

  FunctionType * FSCallInfoTy  = FunctionType::get
    (vpTy, args<const Type*>::list(vpTy, Int32Ty), true);

  addIntrinsic
    ("sc.lscheck",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_MEMCHECK,
     LSCheckTy, 1);

  addIntrinsic
    ("sc.lscheckui",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_MEMCHECK,
     LSCheckTy, 1);

  addIntrinsic
    ("sc.lscheckalign",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_MEMCHECK,
     LSCheckAlignTy, 1);

  addIntrinsic
    ("sc.lscheckalignui",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_MEMCHECK,
     LSCheckAlignTy, 1);

  addIntrinsic
    ("sc.boundscheck",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_BOUNDSCHECK,
     BoundsCheckTy, 2);
  
  addIntrinsic
    ("sc.boundscheckui",
    SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_BOUNDSCHECK,
     BoundsCheckTy, 2);

  addIntrinsic
    ("sc.exactcheck2",
    SC_INTRINSIC_HAS_VALUE_POINTER
    | SC_INTRINSIC_CHECK | SC_INTRINSIC_BOUNDSCHECK,
     ExactCheck2Ty, 1);

  addIntrinsic
    ("sc.funccheck",
     SC_INTRINSIC_HAS_VALUE_POINTER |
     SC_INTRINSIC_CHECK | SC_INTRINSIC_MEMCHECK,
     FuncCheckTy, 1);

  addIntrinsic
    ("sc.get_actual_val",
     SC_INTRINSIC_OOB,
     GetActualValTy, 1);

  addIntrinsic
    ("sc.pool_register",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolRegTy, 1);

  addIntrinsic
    ("sc.pool_register_stack",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolRegTy, 1);

  addIntrinsic
    ("sc.pool_register_global",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolRegTy, 1);

  addIntrinsic
    ("sc.pool_unregister",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolUnregTy, 1);

  addIntrinsic
    ("sc.pool_unregister_stack",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolUnregTy, 1);

  addIntrinsic
    ("sc.pool_unregister_global",
     SC_INTRINSIC_HAS_POOL_HANDLE | SC_INTRINSIC_HAS_VALUE_POINTER
     | SC_INTRINSIC_REGISTRATION,
     PoolUnregTy, 1);

  addIntrinsic
    ("sc.pool_argvregister",
     SC_INTRINSIC_REGISTRATION | SC_INTRINSIC_HAS_VALUE_POINTER,
     PoolArgRegTy, 1);

  addIntrinsic
    ("sc.register_globals",
     SC_INTRINSIC_REGISTRATION,
     RegisterGlobalsTy);

  addIntrinsic
    ("sc.init_runtime",
     SC_INTRINSIC_MISC,
     InitRuntimeTy);

  addIntrinsic
    ("sc.init_pool_runtime",
     SC_INTRINSIC_MISC,
     InitPoolRuntimeTy);

  addIntrinsic
    ("sc.fsparameter",
     SC_INTRINSIC_MISC,
     FSParameterTy);

  addIntrinsic
    ("sc.fscallinfo",
     SC_INTRINSIC_MISC,
     FSCallInfoTy);

  addDebugIntrinsic("sc.lscheck");
  addDebugIntrinsic("sc.lscheckalign");
  addDebugIntrinsic("sc.boundscheck");
  addDebugIntrinsic("sc.boundscheckui");
  addDebugIntrinsic("sc.exactcheck2");
  addDebugIntrinsic("sc.pool_register");

  
  // We always change the module.
  return true;
}

//
// Method: addIntrinsic()
//
// Description:
//  Create and register a new function as a SAFECode intrinsic function.
//
// Inputs:
//  type     - The type of intrinsic check.
//  name     - The name of the function.
//  FTy      - The LLVM type of the intrinsic function.
//  ptrindex - The index of the operand to the function which is used to take
//             the pointer which the intrinsic checks.  This is unused for
//             non-run-time checking intrinsics.
//
//
void
InsertSCIntrinsic::addIntrinsic (const char * name,
                                 unsigned int flag,
                                 FunctionType * FTy,
                                 unsigned ptrindex /* = 0 */) {
  //
  // Check that this pass has already analyzed an LLVM Module.
  //
  assert (currentModule && "No LLVM Module!");

  // Structure describing the new intrinsic function
  IntrinsicInfoTy info;

  //
  // Check to see if the function exists.
  //
  bool hadToCreateFunction = true;
  if (Function * F = currentModule->getFunction(name))
    if (F->getFunctionType() == FTy)
      hadToCreateFunction = false;

  //
  // Create the new intrinsic function and configure its SAFECode attributes.
  //
  info.flag = flag;
  info.F = dyn_cast<Function> (currentModule->getOrInsertFunction(name, FTy));
  info.ptrindex = ptrindex;

  //
  // Give the function a body.  This is used for ensuring that SAFECode plays
  // nicely with LLVM's bugpoint tool.  By having a body, the program will link
  // correctly even when the intrinsic renaming pass is removed by bugpoint.
  //
#if 0
  if (hadToCreateFunction) {
    LLVMContext & Context = getGlobalContext();
    BasicBlock * entryBB=BasicBlock::Create (Context, "entry", info.F);
    const Type * VoidTy = Type::getVoidTy(Context);
    if (FTy->getReturnType() == VoidTy) {
      ReturnInst::Create (Context, entryBB);
    } else {
      Value * retValue = UndefValue::get (FTy->getReturnType());
      ReturnInst::Create (Context, retValue, entryBB);
    }
  }
#endif

  //
  // Map the function name and LLVM Function pointer to its SAFECode attributes.
  //
  intrinsics.push_back (info);
  intrinsicNameMap.insert
    (StringMapEntry<uint32_t>::Create(name, name + strlen(name), intrinsics.size() - 1));
}

const InsertSCIntrinsic::IntrinsicInfoTy &
InsertSCIntrinsic::getIntrinsic(const std::string & name) const {
  StringMap<uint32_t>::const_iterator it = intrinsicNameMap.find(name);
  assert(it != intrinsicNameMap.end() && "Intrinsic should be defined before uses!");
  return intrinsics[it->second];
}

//
// Method: isSCIntrinsicWithFlags()
//
// Description:
//  Determine whether the specified LLVM value is a call to a SAFECode
//  intrinsic with the specified flags.
//
// Inputs:
//  inst - The LLVM Value to check.  It can be any value, including
//         non-instruction values.
//  flag - Indicate the property in the desired intrinsic
//
// Return value:
//  true  - The LLVM value is a call to a SAFECode run-time function and has
//          one or more of the specified flags.
//  false - The LLVM value is not a call to a SAFECode run-time function.
//
bool
InsertSCIntrinsic::isSCIntrinsicWithFlags(Value * inst, unsigned flag) const {
  if (!isa<CallInst>(inst)) 
    return false;

  CallInst * CI = cast<CallInst>(inst);
  Function * F= CI->getCalledFunction();
  if (!F)
    return false;

  const std::string & name = F->getName();


  StringMap<uint32_t>::const_iterator it = intrinsicNameMap.find(name);

  if (it == intrinsicNameMap.end())
    return false;

  const IntrinsicInfoTy & info = intrinsics[it->getValue()];
  return (info.flag & flag);
}

//
// Method: getValuePointer()
//
// Description:
//  This method returns the pointer value that is used in an intrinsic call.
//  For run-time checks, this is usually the pointer that is being checked.
//
// Inputs:
//  CI - The call instruction for which the pointer operand is desired.  Note
//       that can be a non-intrinsic call.
//
// Return value:
//  0 - This call is not a SAFECode intrinsic call or there is no pointer value 
//      associated with this call.
//  Otherwise, a pointer to the pointer value operand is returned.
//
Value *
InsertSCIntrinsic::getValuePointer (CallInst * CI) {
  if (isSCIntrinsicWithFlags (CI, SC_INTRINSIC_HAS_VALUE_POINTER)) {
    const IntrinsicInfoTy & info = intrinsics[intrinsicNameMap[CI->getCalledFunction()->getName()]];

    //
    // Return the checked pointer in the call.  We use ptrindex + 1 because the
    // index is the index in the function signature, but in a CallInst, the
    // first argument is the function pointer.
    //
    return (CI->getOperand(info.ptrindex+1));
  }

  return NULL;
}


//
// Attempt to look for the originally allocated object by scanning the data
// flow up.
//
Value *
InsertSCIntrinsic::findObject(Value * obj) {
  std::set<Value *> exploredObjects;
  std::set<Value *> objects;
  std::queue<Value *> queue;
  queue.push(obj);
  while (!queue.empty()) {
    Value * o = queue.front();
    queue.pop();
    if (exploredObjects.count(o)) continue;

    exploredObjects.insert(o);

    if (isa<CastInst>(o)) {
      queue.push(cast<CastInst>(o)->getOperand(0));
    } else if (isa<GetElementPtrInst>(o)) {
      queue.push(cast<GetElementPtrInst>(o)->getPointerOperand());
    } else if (isa<PHINode>(o)) {
      PHINode * p = cast<PHINode>(o);
      for(unsigned int i = 0; i < p->getNumIncomingValues(); ++i) {
        queue.push(p->getIncomingValue(i));
      }
    } else {
      objects.insert(o);
    }
  }
  return objects.size() == 1 ? *(objects.begin()) : NULL;
}

//
// Check to see if we're indexing off the beginning of a known object.  If
// so, then find the size of the object.  Otherwise, return NULL.
//
Value *
InsertSCIntrinsic::getObjectSize(Value * V) {
  V = findObject(V);
  if (!V) {
    return NULL;
  }

  const Type * Int32Type = IntegerType::getInt32Ty(getGlobalContext());

  if (GlobalVariable * GV = dyn_cast<GlobalVariable>(V)) {
    return ConstantInt::get(Int32Type, TD->getTypeAllocSize (GV->getType()->getElementType()));
  }

  if (AllocaInst * AI = dyn_cast<AllocaInst>(V)) {
    unsigned int type_size = TD->getTypeAllocSize (AI->getAllocatedType());
    if (AI->isArrayAllocation()) {
      if (ConstantInt * CI = dyn_cast<ConstantInt>(AI->getArraySize())) {
        if (CI->getSExtValue() > 0) {
          type_size *= CI->getSExtValue();
        } else {
          return NULL;
        }
      } else {
        return NULL;
      }
    }
    return ConstantInt::get(Int32Type, type_size);
  }

  // Customized allocators

  if (CallInst * CI = dyn_cast<CallInst>(V)) {
    Function * F = CI->getCalledFunction();
    if (!F)
      return NULL;

    const std::string & name = F->getName();
    for (SAFECodeConfiguration::alloc_iterator it = SCConfig.alloc_begin(),
           end = SCConfig.alloc_end(); it != end; ++it) {
      if ((*it)->isAllocSizeMayConstant(CI) && (*it)->getAllocCallName() == name) {
        return (*it)->getAllocSize(CI);
      }
    }
  }

  // Byval function arguments
  if (Argument * AI = dyn_cast<Argument>(V)) {
    if (AI->hasByValAttr()) {
      assert (isa<PointerType>(AI->getType()));
      const PointerType * PT = cast<PointerType>(AI->getType());
      unsigned int type_size = TD->getTypeAllocSize (PT->getElementType());
      return ConstantInt::get (Int32Type, type_size);
    }
  }

  return NULL;
}

char InsertSCIntrinsic::ID = 0;
static llvm::RegisterPass<InsertSCIntrinsic> X ("sc-insert-intrinsic", "insert SAFECode's intrinsic", true, true);

NAMESPACE_SC_END
