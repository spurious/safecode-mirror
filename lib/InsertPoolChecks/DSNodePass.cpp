//===- DSNodePass.cpp - ------------------------------------------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "dsnode"

#include <iostream>
#include "safecode/Config/config.h"
#include "InsertPoolChecks.h"

namespace llvm {

char DSNodePass::ID = 0; 
static llvm::RegisterPass<DSNodePass> passDSNode("ds-node", "Prepare DS Graph and Pool Handle information for SAFECode", false);

bool
DSNodePass::runOnModule(Module & M) {
  std::cerr << "Running DSNodePass" << std::endl; 
#ifndef LLVA_KERNEL  
  paPass = &getAnalysis<PoolAllocateGroup>();
  assert (paPass && "Pool Allocation Transform *must* be run first!");
#if 0
  efPass = &getAnalysis<EmbeCFreeRemoval>();
#endif
#endif
  return false;
}

// Method: getDSGraph()
//
// Description:
//  Return the DSGraph for the given function.  This method automatically
//  selects the correct pass to query for the graph based upon whether we're
//  doing user-space or kernel analysis.
//
DSGraph &
DSNodePass::getDSGraph(Function & F) {
#ifndef LLVA_KERNEL
  return paPass->getDSGraph(F);
#else  
  return TDPass->getDSGraph(F);
#endif  
}

#ifndef LLVA_KERNEL
//
// Method: getPoolHandle()
//
// Description:
//  Return the pool handle assigned to this value.
//
// Inputs:
//  V         - The value for which we seek the pool handle.
//  collapsed - Flags whether we are willing to get pool handles for collapsed
//              pools.
//
// Return value:
//  0 - No pool handle was found.
//  Otherwise, returns either the pool handle or a pointer to a NULL pool
//  handle.
//
// Note:
//  Currently, collapsed is always set to true, meaning that we never use
//  information from the EmbeC pass like in the Olden days (ha ha!).  That
//  code, therefore, is disabled in order to speed up the revival of SAFECode
//  with Automatic Pool Allocation.
//
Value *
DSNodePass::getPoolHandle (const Value *V,
                           Function *F,
                           PA::FuncInfo &FI,
                           bool collapsed) {

  //
  // Ensure that the caller is okay with collapsed pools.  Code below for
  // handling the case when we don't want collapsed pools is disabled to
  // remove dependence on the old EmbeC passes.
  //
  assert (collapsed && "For now, we must always handle collapsed pools!\n");

#if 1
  //
  // JTC:
  //  If this function has a clone, then try to grab the original.
  //
  if (!(paPass->getFuncInfo(*F)))
  {
    F = paPass->getOrigFunctionFromClone(F);
    assert (F && "No Function Information from Pool Allocation!\n");
  }
#endif

  //
  // Get the DSNode for the value.
  //
  const DSNode *Node = getDSNode(V,F);
  if (!Node) {
    std::cerr << "JTC: Value " << *V << " has no DSNode!" << std::endl;
    return 0;
  }

  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  const Type *PoolDescType = paPass->getPoolType();
  const Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);
  if (Node->isUnknownNode()) {
    //
    // FIXME:
    //  This should be in a top down pass or propagated like collapsed pools
    //  below .
    //
    if (!collapsed) {
#if 0
      assert(!getDSNodeOffset(V, F) && " we don't handle middle of structs yet\n");
#else
      if (getDSNodeOffset(V, F))
        std::cerr << "ERROR: we don't handle middle of structs yet"
                  << std::endl;
#endif
std::cerr << "JTC: PH: Null 1: " << *V << std::endl;
      return Constant::getNullValue(PoolDescPtrTy);
    }
  }

  Value * PH = paPass->getPool (Node, *F);

#if 0
  map <Function *, set<Value *> > &
    CollapsedPoolPtrs = efPass->CollapsedPoolPtrs;
#endif
  
  if (PH) {
    // Check that the node pointed to by V in the TD DS graph is not
    // collapsed
#if 0
    if (!collapsed && CollapsedPoolPtrs.count(F)) {
      Value *v = PH;
      if (CollapsedPoolPtrs[F].find(PH) != CollapsedPoolPtrs[F].end()) {
#ifdef DEBUG
        std::cerr << "Collapsed pools \n";
#endif
        return Constant::getNullValue(PoolDescPtrTy);
      } else {
        if (Argument * Arg = dyn_cast<Argument>(v))
          if ((Arg->getParent()) != F)
            return Constant::getNullValue(PoolDescPtrTy);
        return v;
      }
    } else {
#else
    {
#endif
      if (Argument * Arg = dyn_cast<Argument>(PH))
        if ((Arg->getParent()) != F)
          return Constant::getNullValue(PoolDescPtrTy);
      return PH;
    } 
  }

std::cerr << "JTC: Value " << *V << " not in PoolDescriptor List!" << std::endl;
  return 0;
}
#else
Value *
DSNodePass::getPoolHandle(const Value *V, Function *F) {
  DSGraph &TDG =  TDPass->getDSGraph(*F);
  const DSNode *Node = TDG.getNodeForValue((Value *)V).getNode();
  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  //  if (Node->isUnknownNode()) {
  //    return 0;
  //  }
  if ((TDG.getPoolDescriptorsMap()).count(Node)) 
    return (TDG.getPoolDescriptorsMap()[Node]->getMetaPoolValue());
  return 0;
}
#endif

DSNode* DSNodePass::getDSNode (const Value *VOrig, Function *F) {
#ifndef LLVA_KERNEL
  //
  // JTC:
  //  If this function has a clone, then try to grab the original.
  //
  Value * Vnew = (Value *)(VOrig);
  if (!(paPass->getFuncInfo(*F))) {
    F = paPass->getOrigFunctionFromClone(F);
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    if (!FI->NewToOldValueMap.empty()) {
      Vnew = FI->MapValueToOriginal (Vnew);
    }
    assert (F && "No Function Information from Pool Allocation!\n");
  }

  // Ensure that the function has a DSGraph
  assert (paPass->hasDSGraph(*F) && "No DSGraph for function!\n");

  const Value * V = (Vnew) ? Vnew : VOrig;
  DSGraph &TDG = paPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
}

unsigned DSNodePass::getDSNodeOffset(const Value *V, Function *F) {
#ifndef LLVA_KERNEL
  DSGraph &TDG = paPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  return TDG.getNodeForValue((Value *)V).getOffset();
}
}
