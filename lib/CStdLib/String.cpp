//===-------- String.cpp - Secure C standard string library calls ---------===//
//
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass finds all calls to functions in the C standard string library and
// transforms them to a more secure form.
//
//===----------------------------------------------------------------------===//

#include "safecode/CStdLib.h"

NAMESPACE_SC_BEGIN

// Identifier variable for the pass
char StringTransform::ID = 0;

// Statistics counters
#if 0
STATISTIC(stat_transform_memcpy, "Total memcpy() calls transformed");
STATISTIC(stat_transform_memmove, "Total memmove() calls transformed");
STATISTIC(stat_transform_mempcpy, "Total mempcpy() calls transformed");
STATISTIC(stat_transform_memset, "Total memset() calls transformed");
#endif
STATISTIC(stat_transform_strncpy, "Total strncpy() calls transformed");

STATISTIC(stat_transform_strcpy, "Total strcpy() calls transformed");
#if 0
STATISTIC(stat_transform_strlcat, "Total strlcat() calls transformed");
STATISTIC(stat_transform_strlcpy, "Total strlcpy() calls transformed");
#endif
STATISTIC(stat_transform_strlen, "Total strlen() calls transformed");
STATISTIC(stat_transform_strnlen, "Total strnlen() calls transformed");

STATISTIC(stat_transform_strchr,  "Total strchr() calls transformed");
STATISTIC(stat_transform_strrchr, "Total strrchr() calls transformed");
STATISTIC(stat_transform_strcat,  "Total strcat() calls transformed");
STATISTIC(stat_transform_strncat, "Total strncat() calls transformed");
STATISTIC(stat_transform_strstr,  "Total strstr() calls transformed");
STATISTIC(stat_transform_strpbrk, "Total strpbrk() calls transformed");

STATISTIC(stat_transform_strcmp, "Total strcmp() calls transformed");
#if 0
STATISTIC(stat_transform_wcscpy, "Total wcscpy() calls transformed");
STATISTIC(stat_transform_wmemcpy, "Total wmemcpy() calls transformed");
STATISTIC(stat_transform_wmemmove, "Total wmemmove() calls transformed");
#endif

static RegisterPass<StringTransform>
ST("string_transform", "Secure C standard string library calls");

#define DSNODE_NOT_COMPLETE (DSNode::ExternalNode | DSNode::IncompleteNode | DSNode::UnknownNode)

/**
 * Entry point for the LLVM pass that transforms C standard string library calls
 *
 * @param	M	Module to scan
 * @return	Whether we modified the module
 */
bool StringTransform::runOnModule(Module &M) {
  // Flags whether we modified the module.
  bool modified = false;

  tdata = &getAnalysis<TargetData>();

  dsaPass = &getAnalysis<EQTDDataStructures>();
  assert(dsaPass && "Must run DSA Pass first!");

  // Create needed pointer types (char * == i8 * == VoidPtrTy).
  const Type *Int8Ty  = IntegerType::getInt8Ty(M.getContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);
  // Determine the size of size_t for functions that return this result.
  const Type *SizeTTy = tdata->getIntPtrType(M.getContext());
  // Determine the size of int for functions like strcmp, strncmp and etc..
  const Type *Int32ty = IntegerType::getInt32Ty(M.getContext());

  modified |= transform(M, "strcpy",  2, 2, VoidPtrTy, stat_transform_strcpy);
  modified |= transform(M, "strncpy", 3, 2, VoidPtrTy, stat_transform_strncpy);
  modified |= transform(M, "strlen",  1, 1, SizeTTy, stat_transform_strlen);
  modified |= transform(M, "strnlen", 2, 1, SizeTTy, stat_transform_strnlen);

  modified |= transform(M, "strchr",  2, 1, VoidPtrTy, stat_transform_strchr);
  modified |= transform(M, "strrchr", 2, 1, VoidPtrTy, stat_transform_strrchr);
  modified |= transform(M, "strcat",  2, 2, VoidPtrTy, stat_transform_strcat);
  modified |= transform(M, "strncat", 3, 2, VoidPtrTy, stat_transform_strncat);
  modified |= transform(M, "strstr",  2, 2, VoidPtrTy, stat_transform_strstr);
  modified |= transform(M, "strpbrk", 2, 2, VoidPtrTy, stat_transform_strpbrk);
  modified |= transform(M, "strcmp", 2, 2, Int32ty, stat_transform_strcmp);

#if 0
  modified |= transform(M, "memcpy", 3, 2, VoidPtrTy, stat_transform_memcpy);
  modified |= transform(M, "memmove", 3, 2, VoidPtrTy, stat_transform_memmove);
  modified |= transform(M, "mempcpy", 3, 2, VoidPtrTy, stat_transform_mempcpy);
  modified |= transform(M, "memset", 3, 1, VoidPtrTy, stat_transform_memset);
#endif


  return modified;
}

/**
 * Secures C standard string library call by transforming with DSA information
 *
 * @param	M	Module from runOnModule() to scan for functions to transform
 * @return	Whether we modified the module
 */
bool StringTransform::transform(Module &M, const StringRef FunctionName, const unsigned argc, const unsigned pool_argc, const Type *ReturnTy, Statistic &statistic) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create void pointer type for null pool handle.
  const Type *Int8Ty = IntegerType::getInt8Ty(M.getContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F = M.getFunction(FunctionName);
  if (!F)
    return modified;

  // Scan through the module for the desired function to transform.
  for (Value::use_iterator UI = F->use_begin(), UE = F->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      unsigned char complete = 0;
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != argc) {
        I->dump();
        assert(CS.arg_size() == argc && "Incorrect number of arguments!");
        continue;
      }

      // Check for correct return type.
      if (CalledF->getReturnType() != ReturnTy)
        continue;

      Function *F_DSA = I->getParent()->getParent();

      // Create null pool handles.
      Value *PH = ConstantPointerNull::get(VoidPtrTy);

      SmallVector<Value *, 6> Params;

      if (pool_argc == 1) {
        if (getDSFlags(I->getOperand(1), F_DSA) & DSNODE_NOT_COMPLETE) {
          complete |= 0x01; // Clear bit 0
        }

        Params.push_back(PH);
        Params.push_back(I->getOperand(1));
      } else if (pool_argc == 2) {
        if (getDSFlags(I->getOperand(1), F_DSA) & DSNODE_NOT_COMPLETE) {
          complete |= 0x01; // Clear bit 0
        }

        if (getDSFlags(I->getOperand(2), F_DSA) & DSNODE_NOT_COMPLETE) {
          complete |= 0x02; // Clear bit 1
        }

        Params.push_back(PH);
        Params.push_back(PH);
        Params.push_back(I->getOperand(1));
        Params.push_back(I->getOperand(2));
      } else {
        assert("Unsupported number of pool arguments!");
        continue;
      }

      // FunctionType::get() needs std::vector<>
      std::vector<const Type *> ParamTy(2 * pool_argc, VoidPtrTy);

      const Type *Ty = NULL;

      // Add the remaining (non-pool) arguments.
      if (argc > pool_argc) {
        unsigned i = argc - pool_argc;

        for (; i > 0; --i) {
          Params.push_back(I->getOperand(argc - i + 1));
          Ty = I->getOperand(argc - i + 1)->getType();
          ParamTy.push_back(Ty);
        }
      }

      // Add the DSA completeness bitwise vector.
      Params.push_back(ConstantInt::get(Int8Ty, complete));
      ParamTy.push_back(Int8Ty);

      // Construct the transformed function.
      FunctionType *FT = FunctionType::get(F->getReturnType(), ParamTy, false);
      Constant *F_pool = M.getOrInsertFunction("pool_" + FunctionName.str(), FT);

      // Create the call instruction for the transformed function and insert it before the current instruction.
      CallInst *CI = CallInst::Create(F_pool, Params.begin(), Params.end(), "", I);
      
      // Replace all uses of the function with its transformed equivalent.
      I->replaceAllUsesWith(CI);
      I->eraseFromParent();

      // Record the transform.
      ++statistic;

      // Mark the module as modified and continue to the next function call.
      modified = true;
    }
  }

  return modified;
}

// Method: getDSFlags()
//
// Description:
//  Return the DSNode flags associated with the specified value.
//
// Inputs:
//  V - The value for which the DSNode flags are requested.  This value *must*
//      have a DSNode.
//
// Return Value:
//  The DSNode flags (which are a vector of bools in an unsigned int).
//
unsigned StringTransform::getDSFlags(const Value *V, const Function *F) {
  // Ensure that the function has a DSGraph
  assert(dsaPass->hasDSGraph(*F) && "No DSGraph for function!\n");

  // Lookup the DSNode for the value in the function's DSGraph.
  DSGraph *TDG = dsaPass->getDSGraph(*F);
  DSNodeHandle DSH = TDG->getNodeForValue(V);

  // If the value wasn't found in the function's DSGraph, then maybe we can
  // find the value in the globals graph.
  if (DSH.isNull() && isa<GlobalValue>(V)) {
    // Try looking up this DSNode value in the globals graph.  Note that
    // globals are put into equivalence classes; we may need to first find the
    // equivalence class to which our global belongs, find the global that
    // represents all globals in that equivalence class, and then look up the
    // DSNode Handle for *that* global.
    DSGraph *GlobalsGraph = TDG->getGlobalsGraph();
    DSH = GlobalsGraph->getNodeForValue(V);
    if (DSH.isNull()) {
      // DSA does not currently handle global aliases.
      if (!isa<GlobalAlias>(V)) {
        // We have to dig into the globalEC of the DSGraph to find the DSNode.
        const GlobalValue *GV = dyn_cast<GlobalValue>(V);
        const GlobalValue *Leader;
        Leader = GlobalsGraph->getGlobalECs().getLeaderValue(GV);
        DSH = GlobalsGraph->getNodeForValue(Leader);
      }
    }
  }

  // Get the DSNode for the value.
  DSNode *DSN = DSH.getNode();
  assert(DSN && "getDSFlags(): No DSNode for the specified value!\n");

#if 0
  if (DSN->isNodeCompletelyFolded()) {
  }
#endif

  // Return its flags
  return DSN->getNodeFlags();
}

NAMESPACE_SC_END
