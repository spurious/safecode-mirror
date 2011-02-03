//===- SafeLoadStoreOpts.cpp - Optimize safe load/store checks ------------ --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass removes load/store checks that are known to be safe statically.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "opt-safecode"

#include "safecode/SafeLoadStoreOpts.h"

#include "llvm/ADT/Statistic.h"

NAMESPACE_SC_BEGIN

char OptimizeSafeLoadStore::ID = 0;

static RegisterPass<OptimizeSafeLoadStore>
X ("opt-safels", "Remove safe load/store runtime checks");

// Pass Statistics
namespace {
  STATISTIC (TypeSafeChecksRemoved , "Type-safe Load/Store Checks Removed");
  STATISTIC (TrivialChecksRemoved ,  "Trivial Load/Store Checks Removed");
}

//
// Method: getDSNodeHandle()
//
// Description:
//  This method looks up the DSNodeHandle for a given LLVM value.  The context
//  of the value is the specified function, although if it is a global value,
//  the DSNodeHandle may exist within the global DSGraph.
//
// Return value:
//  A DSNodeHandle for the value is returned.  This DSNodeHandle could either
//  be in the function's DSGraph or from the GlobalsGraph.  Note that the
//  DSNodeHandle may represent a NULL DSNode.
//
DSNodeHandle
OptimizeSafeLoadStore::getDSNodeHandle (const Value * V, const Function * F) {
  //
  // Get access to the points-to results.
  //
  EQTDDataStructures & dsaPass = getAnalysis<EQTDDataStructures>();

  //
  // Ensure that the function has a DSGraph
  //
  assert (dsaPass.hasDSGraph(*F) && "No DSGraph for function!\n");

  //
  // Lookup the DSNode for the value in the function's DSGraph.
  //
  DSGraph * TDG = dsaPass.getDSGraph(*F);
  DSNodeHandle DSH = TDG->getNodeForValue(V);

  //
  // If the value wasn't found in the function's DSGraph, then maybe we can
  // find the value in the globals graph.
  //
  if ((DSH.isNull()) && (isa<GlobalValue>(V))) {
    //
    // Try looking up this DSNode value in the globals graph.  Note that
    // globals are put into equivalence classes; we may need to first find the
    // equivalence class to which our global belongs, find the global that
    // represents all globals in that equivalence class, and then look up the
    // DSNode Handle for *that* global.
    //
    DSGraph * GlobalsGraph = TDG->getGlobalsGraph ();
    DSH = GlobalsGraph->getNodeForValue(V);
    if (DSH.isNull()) {
      //
      // DSA does not currently handle global aliases.
      //
      if (!isa<GlobalAlias>(V)) {
        //
        // We have to dig into the globalEC of the DSGraph to find the DSNode.
        //
        const GlobalValue * GV = dyn_cast<GlobalValue>(V);
        const GlobalValue * Leader;
        Leader = GlobalsGraph->getGlobalECs().getLeaderValue(GV);
        DSH = GlobalsGraph->getNodeForValue(Leader);
      }
    }
  }

  return DSH;
}

bool
OptimizeSafeLoadStore::runOnModule(Module & M) {
  //
  // Get access to prerequisite passes.
  //
  InsertSCIntrinsic & intrinsic = getAnalysis<InsertSCIntrinsic>();
  dsa::TypeSafety<EQTDDataStructures> & TS = getAnalysis<dsa::TypeSafety<EQTDDataStructures> >();

  //
  // Scan through all uses of the complete run-time check and record any checks
  // on type-known pointers.  These can be removed.
  //
  std::vector <CallInst *> toRemoveTypeSafe;
  std::vector <CallInst *> toRemoveObvious;
  Function * LSCheck   = M.getFunction ("sc.lscheck");
  Value::use_iterator UI = LSCheck->use_begin();
  Value::use_iterator  E = LSCheck->use_end();
  for (; UI != E; ++UI) {
    if (CallInst * CI = dyn_cast<CallInst>(*UI)) {
      if (CI->getCalledValue()->stripPointerCasts() == LSCheck) {
        //
        // Get the pointer that is checked by this run-time check.
        //
        Value * CheckPtr = intrinsic.getValuePointer (CI)->stripPointerCasts();

        //
        // If it is obvious that the pointer is within a valid object, then
        // remove the check.
        //
        if ((isa<AllocaInst>(CheckPtr)) || isa<GlobalVariable>(CheckPtr)) {
            toRemoveObvious.push_back (CI);
            continue;
        }

        //
        // If the pointer is complete, then remove the check if it points to
        // a type-consistent object.
        //
        Function * F = CI->getParent()->getParent();
        if (TS.isTypeSafe (CheckPtr, F)) {
          toRemoveTypeSafe.push_back (CI);
          continue;
        }
      }
    }
  }

  //
  // Update statistics.  Note that we only assign if the value is non-zero;
  // this prevents the statistics from being reported if the value is zero.
  //
  bool modified = false;
  if (toRemoveTypeSafe.size()) {
    TypeSafeChecksRemoved += toRemoveTypeSafe.size();
    modified = true;
  }

  if (toRemoveObvious.size()) {
    TrivialChecksRemoved += toRemoveObvious.size();
    modified = true;
  }

  //
  // Now iterate through all of the call sites and transform them to be
  // complete.
  //
  for (unsigned index = 0; index < toRemoveObvious.size(); ++index) {
    toRemoveObvious[index]->eraseFromParent();
  }

  for (unsigned index = 0; index < toRemoveTypeSafe.size(); ++index) {
    toRemoveTypeSafe[index]->eraseFromParent();
  }

  return modified;
}

NAMESPACE_SC_END
