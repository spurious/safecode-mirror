//===- FormatStrings.cpp - Secure calls to printf/scanf style functions ---===//
// 
//                            The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass to insert calls to runtime wrapper functions for
// printf() and related format string functions.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "formatstrings"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/IRBuilder.h"

#include "safecode/FormatStrings.h"
#include "safecode/VectorListHelper.h"

#include <set>
#include <map>
#include <vector>
#include <algorithm>

using namespace llvm;

using std::map;
using std::max;
using std::set;
using std::vector;

NAMESPACE_SC_BEGIN

static RegisterPass<FormatStringTransform>
R("formatstrings", "Secure calls to format string functions");

STATISTIC(stat_printf,   "Number of calls to printf() that were secured");
STATISTIC(stat_fprintf,  "Number of calls to fprintf() that were secured");
STATISTIC(stat_sprintf,  "Number of calls to sprintf() that were secured");
STATISTIC(stat_snprintf, "Number of calls to snprintf() that were secured");
STATISTIC(stat_err,      "Number of calls to err() that were secured");
STATISTIC(stat_errx,     "Number of calls to errx() that were secured");
STATISTIC(stat_warn,     "Number of calls to warn() that were secured");
STATISTIC(stat_warnx,    "Number of calls to warnx() that were secured");
STATISTIC(stat_syslog,   "Number of calls to syslog() that were secured");
STATISTIC(stat_scanf,    "Number of calls to scanf() that were secured");
STATISTIC(stat_fscanf,   "Number of calls to fscanf() that were secured");
STATISTIC(stat_sscanf,   "Number of calls to sscanf() that were secured");

char FormatStringTransform::ID = 0;

//
// Constructs a FunctionType which is consistent with the type of a tranformed
// format string function.
//
// Inputs:
//   C    - the context to build types from
//   argc - the expected number of (fixed) arguments the function type takes
//   F    - the original function type
//
FunctionType *
FormatStringTransform::buildTransformedFunctionType(LLVMContext &C,
                                                    unsigned argc,
                                                    const FunctionType *F)
{
  const Type *int8ptr = Type::getInt8PtrTy(C);
  FunctionType::param_iterator i     = F->param_begin();
  FunctionType::param_iterator end   = F->param_end();
  vector<const Type *> NewParams;

  assert(F->getNumParams() == argc && "Incorrect number of argument!");

  NewParams.push_back(int8ptr);
  while (i != end)
  {
    if (isa<PointerType>((Type *) *i))
      NewParams.push_back(int8ptr);
    else
      NewParams.push_back(*i);
    ++i;
  }

  return FunctionType::get(F->getReturnType(), NewParams, true);
}

bool
FormatStringTransform::runOnModule(Module &M)
{
  //
  // Get the intrinsics we will use.
  //
  InsertSCIntrinsic &I = getAnalysis<InsertSCIntrinsic>();
  FSParameter = I.getIntrinsic("sc.fsparameter").F;
  FSCallInfo  = I.getIntrinsic("sc.fscallinfo").F;
  //
  // Get the type of the pointer_info structure.
  //
  makePointerInfoType(M.getContext());

  bool changed = false;

  changed |= transform(M, "printf",   1, "pool_printf",   stat_printf);
  changed |= transform(M, "fprintf",  2, "pool_fprintf",  stat_fprintf);
  changed |= transform(M, "sprintf",  2, "pool_sprintf",  stat_sprintf);
  changed |= transform(M, "snprintf", 3, "pool_snprintf", stat_snprintf);
  changed |= transform(M, "err",      2, "pool_err",      stat_err);
  changed |= transform(M, "errx",     2, "pool_errx",     stat_errx);
  changed |= transform(M, "warn",     1, "pool_warn",     stat_warn);
  changed |= transform(M, "warnx",    1, "pool_warnx",    stat_warnx);
  changed |= transform(M, "syslog",   2, "pool_syslog",   stat_syslog);
  changed |= transform(M, "scanf",    1, "pool_scanf",    stat_scanf);
  changed |= transform(M, "fscanf",   2, "pool_fscanf",   stat_fscanf);
  changed |= transform(M, "sscanf",   2, "pool_sscanf",   stat_sscanf);

  //
  // The transformations use placehold arrays of size 0. This call changes
  // those arrays to be allocated to the proper size.
  //
  if (changed)
    fillArraySizes(M);

  return changed;
}

//
// Transform all calls of a given function into their secured analogue.
//
// A format string function of the form
//
//   int xprintf(arg1, arg2, ...);
//
// will be transformed into a call of the function of the form
//
//   int pool_xprintf(call_info *, arg1, arg2, ...);
//
// with the call_info * structure containing information about the vararg
// arguments passed into the call. All pointer arguments to the call will
// be wrapped around a pointer_info structure. The space for the call_info
// and pointer_info structures is allocated on the stack.
//
//
// Inputs:
//  M           - a reference to the current Module
//  name        - the name of the function to transform
//  argc        - the number of (fixed) arguments to the function
//  replacement - the name of the replacemant function
//  stat        - a statistic pertaining to the number of transfomations
//                that have been performed
//
// This function returns true if the module was modified, false otherwise.
//
bool
FormatStringTransform::transform(Module &M,
                                 const char *name,
                                 unsigned argc,
                                 const char *replacement,
                                 Statistic &stat)
{
  Function *f = M.getFunction(name);

  if (f == 0)
    return false;

  vector<CallInst *> CallInstructions;

  //
  // Locate all the instructions which call the named function.
  //
  for (Function::use_iterator i = f->use_begin();
       i != f->use_end();
       ++i)
  {
    CallInst *I;
    if (((I = dyn_cast<CallInst>(*i)) && I->getCalledFunction() != f) || I == 0)
      continue;
    CallInstructions.push_back(I);
  }

  if (CallInstructions.empty())
    return false;

  FunctionType *rType = buildTransformedFunctionType(f->getContext(),
                                                     argc,
                                                     f->getFunctionType());
  Function *found = M.getFunction(replacement);
  assert((found == 0 || found->getFunctionType() == rType) && 
    "Replacement function already declared in module with incorrect type");

  Value *replacementFunc = M.getOrInsertFunction(replacement, rType);

  //
  // Iterate over the found call instructions and replace them with the
  // transformed calls.
  //
  for (vector<CallInst*>::iterator i = CallInstructions.begin();
       i != CallInstructions.end();
       ++i)
  {
    CallInst *OldCall = *i;
    CallInst *NewCall = buildSecuredCall(replacementFunc, *OldCall);
    NewCall->insertBefore(OldCall);
    OldCall->replaceAllUsesWith(NewCall);
    OldCall->eraseFromParent();

    ++stat;
  }

  return true;
}

//
// Goes over all the arrays that were allocated as helpers to the intrinsics
// and makes them the proper size.
//
void
FormatStringTransform::fillArraySizes(Module &M)
{
  LLVMContext &C = M.getContext();
  IRBuilder<> builder(C);
  const Type *int8ptr = Type::getInt8PtrTy(C);
  const Type *int32 = Type::getInt32Ty(C);

  //
  // Make the CallInfo structure allocations the right size.
  //
  for (map<Function *, unsigned>::iterator i = CallInfoStructUsage.begin();
       i != CallInfoStructUsage.end();
       ++i)
  {
    Function *f = i->first;
    unsigned count = i->second;
    const Type *CIType = makeCallInfoType(C, count);
    AllocaInst *newAlloc = builder.CreateAlloca(CIType);
    Instruction *newCast = cast<Instruction>(
      builder.CreateBitCast(newAlloc, int8ptr)
    );

    //
    // The CallInfo structure is cast to i8* before being passed into any
    // function calls.
    //
    // The placeholder cast is located in CallInfoStructures.
    //
    Instruction *oldCast  = CallInfoStructures[f];
    Instruction *oldAlloc = cast<Instruction>(oldCast->getOperand(0));

    newAlloc->insertBefore(oldAlloc);
    newCast->insertAfter(newAlloc);
    oldCast->replaceAllUsesWith(newCast);

    oldCast->eraseFromParent();
    oldAlloc->eraseFromParent();
  }

  //
  // Make the PointerInfo structure array allocations the right size.
  //
  for (map<Function *, unsigned>::iterator i = PointerInfoFuncArrayUsage.begin();
       i != PointerInfoFuncArrayUsage.end();
       ++i)
  {
    Function *f = i->first;
    Instruction *oldAlloc  = PointerInfoStructures[f];

    Value *sz = ConstantInt::get(int32, i->second);
    AllocaInst *newAlloc = builder.CreateAlloca(PointerInfoType, sz);
    newAlloc->insertBefore(oldAlloc);
    oldAlloc->replaceAllUsesWith(newAlloc);
    oldAlloc->eraseFromParent();
  }

}


//
// Builds a call to sc.fsparameter which registers the given parameter as a
// pointer.
//
// Inputs:
//  i         - the instruction associated with the pointer parameter
//  parameter - the pointer parameter to register
//
// The function inserts the call to sc.fsparameter before the instruction i.
// Since only one call is needed to sc.fsparameter per pointer and instruction,
// the function keeps track of redundant calls to itself and returns the same
// Value each time.
//
// Output:
// The function returns a Value which is the result of wrapping the pointer
// parameter using sc.fsparameter. The type is i8 *.
//
Value *
FormatStringTransform::registerPointerParameter(Instruction *i,
                                                Value *parameter)
{
  //
  // Determine if the value has already been registered for this instruction.
  //
  PointerInfoForParameter index(i, parameter);

  if (FSParameterCalls.find(index) != FSParameterCalls.end())
    return FSParameterCalls[index];

  Function *f = i->getParent()->getParent();
  IRBuilder<> builder(i->getContext());

  //
  // Otherwise use the next free PointerInfo structure.
  //
  // First determine if the array of PointerInfo structures has already
  // been allocated on the function's stack. If not, do so.
  //
  if (PointerInfoStructures.find(f) == PointerInfoStructures.end())
  {
    Value *zero = ConstantInt::get(Type::getInt32Ty(i->getContext()), 0);
    AllocaInst *allocation = builder.CreateAlloca(PointerInfoType, zero);

    //
    // Allocate the array at the entry point of the function.
    //
    BasicBlock::InstListType &instList =
      i->getParent()->getParent()->getEntryBlock().getInstList();
    instList.insert(instList.begin(), allocation);

    PointerInfoStructures[f] = allocation;
    PointerInfoFuncArrayUsage[f] = 0;
  }

  if (PointerInfoArrayUsage.find(i) == PointerInfoArrayUsage.end())
    PointerInfoArrayUsage[i] = 0;

  //
  // Index into the next free position in the PointerInfo array.
  //
  const unsigned nextStructure = PointerInfoArrayUsage[i]++;
  const Type *int8     = Type::getInt8Ty(i->getContext());
  const Type *int8ptr  = Type::getInt8PtrTy(i->getContext());

  //
  // Update the per-function count of the number of pointer_info structures
  // that are used. This used is for allocating the correct size on the stack in
  // fillArraySizes().
  //
  PointerInfoFuncArrayUsage[f] =
    max(PointerInfoFuncArrayUsage[f], 1 + nextStructure);

  Value *array = PointerInfoStructures[f];

  Instruction *gep = cast<Instruction>(
    builder.CreateConstGEP1_32(array, nextStructure)
  );
  Instruction *bitcast = cast<Instruction>(
    builder.CreateBitCast(gep, int8ptr)
  );

  gep->insertBefore(i);
  bitcast->insertBefore(i);

  //
  // Create the sc.fsparameter call and insert it before the given instruction.
  // Also store it for later use if necessary (if the same parameter is
  // registered for the same instruction)
  //
  Value *castedParameter = parameter;
  if (castedParameter->getType() != int8ptr)
  {
    castedParameter = builder.CreateBitCast(parameter, int8ptr);
    if (isa<Instruction>(castedParameter))
      cast<Instruction>(castedParameter)->insertBefore(i);
  }

  vector<Value *> FSArgs(4);
  FSArgs[0] = ConstantPointerNull::get(cast<PointerType>(int8ptr));
  FSArgs[1] = castedParameter;
  FSArgs[2] = bitcast;
  FSArgs[3] = ConstantInt::get(int8, 0);

  CallInst *FSCall =
    builder.CreateCall(FSParameter, FSArgs.begin(), FSArgs.end());
  FSCall->insertBefore(i);

  FSParameterCalls[index] = FSCall;

  return FSCall;
}

//
// Builds a call to sc.callinfo which registers information about the given
// call to a format string function.
//
// Inputs:
//  i       - the instruction associated with the call to the format string
//            function
//  vargc   - the number of variable arguments in the call to register
//  PVArguments - every variable pointer argument to the call of the format
//                string function that should be whitelisted
//                 
// This function returns a Value suitable as the first parameter to a
// transformed format string function like pool_printf.
//
Value *
FormatStringTransform::registerCallInformation(Instruction *i,
                                               uint32_t vargc,
                                               const set<Value *> &PVArguments)
{
  IRBuilder<> builder(i->getContext());

  const unsigned pargc = PVArguments.size();
  const Type *int8ptr  = Type::getInt8PtrTy(i->getContext());

  Function *f = i->getParent()->getParent();

  //
  // Allocate the CallInfo structure at the entry point of the function if
  // necessary. The allocated structure will be a placeholder.
  //
  if (CallInfoStructures.find(f) == CallInfoStructures.end())
  {
    Value *zero = ConstantInt::get(Type::getInt32Ty(i->getContext()), 0);
    const Type *CInfoType = makeCallInfoType(i->getContext(), 0);
    AllocaInst *allocation = builder.CreateAlloca(CInfoType, zero);

    //
    // Put this CallInfo structure on the stack at the function entry.
    //
    BasicBlock::InstListType &instList =
      i->getParent()->getParent()->getEntryBlock().getInstList();

    instList.insert(instList.begin(), allocation);

    //
    // Bitcast it into (i8 *) because that is the type for which it is used as
    // a parameter to sc.fscallinfo.
    //
    Instruction *bitcast = cast<Instruction>(
      builder.CreateBitCast(allocation, int8ptr)
    );
    bitcast->insertAfter(allocation);

    CallInfoStructures[f]  = bitcast;
    CallInfoStructUsage[f] = 0;
  }

  //
  // Update the per-function count of the max size of the whitelist in the
  // call_info structure. Later fillArraySizes() will allocate a structure
  // with enough space to hold a whitelist for each registered
  // call in the function.
  //
  CallInfoStructUsage[f] = max(CallInfoStructUsage[f], pargc);

  Value *cInfo = CallInfoStructures[f];

  Value *null = ConstantPointerNull::get(cast<PointerType>(int8ptr));
  vector<Value *> Params;

  Params.push_back(cInfo);

  Params.push_back(
    ConstantInt::get(Type::getInt32Ty(i->getContext()), vargc)
  );

  //Params.push_back(null);

  set<Value *>::const_iterator start = PVArguments.begin();
  set<Value *>::const_iterator end   = PVArguments.end();
  while (start != end)
  {
    Params.push_back(*start);
    ++start;
  }
  //
  // Append NULL to terminate the variable argument list and finally build the
  // completed call instruction.
  //
  Params.push_back(null);
  CallInst *c = builder.CreateCall(FSCallInfo, Params.begin(), Params.end());
  c->insertBefore(i);

  return c;
}

//
// Builds a call instruction to newFunc out of the existing call instruction.
// The new call uses the same arguments as the old call, except that pointer
// arguments to the old call are first wrapped using sc.fsparameter before
// being passed into the new call.
//
CallInst *
FormatStringTransform::buildSecuredCall(Value *newFunc,
                                        CallInst &oldCall)
{
  set<Value *> pointerVArgs;

  const unsigned fargc =
    oldCall.getCalledFunction()->getFunctionType()->getNumParams();
  const unsigned argc  = oldCall.getNumOperands() - 1;
  const unsigned vargc = argc - fargc;

  vector<Value *> NewArgs(1);

  //
  // Build the parameters to the new call, creating wrappers with
  // sc.fsparameter when necessary.
  //

  for (unsigned i = 1; i <= argc; ++i)
  {
    Value *arg = oldCall.getOperand(i);
    if (!isa<PointerType>(arg->getType()))
    {
      NewArgs.push_back(arg);
      continue;
    }
    Value *wrapped = registerPointerParameter(&oldCall, arg);
    NewArgs.push_back(wrapped);
    //
    // If this is a variable pointer argument, it should be registered with
    // sc.callinfo.
    //
    if (i > fargc)
      pointerVArgs.insert(wrapped);
  }

  //
  // Build the CallInfo structure to the new call.
  //
  NewArgs[0] = registerCallInformation(&oldCall, vargc, pointerVArgs);

  //
  // Construct the new call instruction.
  //
  return CallInst::Create(newFunc, NewArgs.begin(), NewArgs.end());
}

//
// Creates and stores the type of the PointerInfo structure.
// This is defined in FormatStringRuntime.h as
//
//   typedef struct
//   {
//      void *ptr;
//      void *pool;
//      void *bounds[2];
//      uint8_t flags;
//   } pointer_info;
//
// The fields are used as follows:
//  - ptr holds the pointer parameter that was passed.
//  - pool holds the pool that ptr belongs to.
//  - bounds are intended to be filled at runtime with the memory object
//    boundaries of ptr.
//  - flags holds various information about the pointer, regarding completeness
//    etc.
//
const Type *
FormatStringTransform::makePointerInfoType(LLVMContext &C)
{
  const Type *int8         = Type::getInt8Ty(C);
  const Type *int8ptr      = Type::getInt8PtrTy(C);
  const Type *int8ptr_arr2 = ArrayType::get(int8ptr, 2);
  vector<const Type *> PointerInfoFields =
    args<const Type *>::list(int8ptr, int8ptr, int8ptr_arr2, int8);

  return (PointerInfoType = StructType::get(C, PointerInfoFields));
}

//
// Creates the type of the CallInfo structure, with a varying whitelist field
// size.
//
// This type is defined in FormatStringRuntime.h as
//
//   typedef struct
//   {
//      uint32_t vargc;
//      uint32_t tag;
//      uint32_t line_no;
//      const char *source_info;
//      void  *whitelist[1];
//   } call_info;
//
// The fields are used as follows:
//  - vargc is the total number of variable arguments passed in the call.
//  - tag, line_no, source_info hold debug-related information.
//  - whitelist is a variable-sized array of pointers, with the last element
//    in the array being NULL. These pointers are the only values which the
//    wrapper callee will treat as vararg pointer arguments.
//
const Type *
FormatStringTransform::makeCallInfoType(LLVMContext &C, unsigned argc)
{
  const Type *int32       = Type::getInt32Ty(C);
  const Type *int8ptr     = Type::getInt8PtrTy(C);
  const Type *int8ptr_arr = ArrayType::get(int8ptr, 1 + argc);
  vector<const Type *> CallInfoFields =
    args<const Type *>::list(int32, int32, int32, int8ptr, int8ptr_arr);

  return StructType::get(C, CallInfoFields);
}

NAMESPACE_SC_END
