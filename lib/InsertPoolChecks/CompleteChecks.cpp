//===- CompleteChecks.cpp - Make run-time checks complete ----------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass instruments loads and stores with run-time checks to ensure memory
// safety.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "safecode"

#include "safecode/CompleteChecks.h"

#include "llvm/ADT/Statistic.h"

NAMESPACE_SC_BEGIN

char CompleteChecks::ID = 0;

static RegisterPass<CompleteChecks>
X ("compchecks", "Make run-time checks complete");

// Pass Statistics
namespace {
  STATISTIC (CompLSChecks, "Complete Load/Store Checks");
}

// List of run-time checks that need to be changed to complete
static const char * checks[] = {
  "sc.lscheck",
  "sc.boundscheck",
  0
};

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
CompleteChecks::getDSNodeHandle (const Value * V, const Function * F) {
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

//
// Function: makeComplete()
//
// Description:
//  Find run-time checks on memory objects for which we have complete analysis
//  information and change them into complete functions.
//
// Inputs:
//  Complete   - A pointer to the complete run-time check.
//  Incomplete - A pointer to the incomplete run-time check.
//
void
CompleteChecks::makeComplete (Function * Complete, Function * Incomplete) {
  //
  // Scan through all uses of the run-time check and record any checks on
  // complete pointers.
  //
  std::vector <CallInst *> toChange;
  Value::use_iterator UI = Incomplete->use_begin();
  Value::use_iterator  E = Incomplete->use_end();
  for (; UI != E; ++UI) {
    if (CallInst * CI = dyn_cast<CallInst>(*UI)) {
      if (CI->getCalledValue()->stripPointerCasts() == Incomplete) {
        //
        // Get the pointer that is checked by this run-time check.
        //
        Value * CheckPtr = intrinsic->getValuePointer (CI);

        //
        // If the pointer is complete, then change the check.
        //
        Function * F = CI->getParent()->getParent();
        if (DSNode * N = getDSNodeHandle (CheckPtr, F).getNode()) {
          if (!(N->isExternalNode() ||
                N->isIncompleteNode() ||
                N->isUnknownNode() ||
                N->isIntToPtrNode() ||
                N->isPtrToIntNode())) {
            toChange.push_back (CI);
          }
        }
      }
    }
  }

  //
  // Update statistics.  Note that we only assign if the value is non-zero;
  // this prevents the statistics from being reported if the value is zero.
  //
  if (toChange.size())
    CompLSChecks += toChange.size();

  //
  // Now iterate through all of the call sites and transform them to be
  // complete.
  //
  for (unsigned index = 0; index < toChange.size(); ++index) {
    toChange[index]->setCalledFunction (Complete);
  }

  return;
}

bool
CompleteChecks::runOnModule (Module & M) {
  //
  // Get pointers to required analysis passes.
  //
  intrinsic = &getAnalysis<InsertSCIntrinsic>();

  //
  // For every run-time check, go and see if it can be converted into a
  // complete check.
  //
  for (unsigned index = 0; (checks[index] != 0); ++index) {
    //
    // Get a pointer to the complete and incomplete versions of the run-time
    // check.
    //
    Function * Complete   = M.getFunction (checks[index]);
    Function * Incomplete = M.getFunction (std::string(checks[index]) + "ui");
    makeComplete (Complete, Incomplete);
  }
  return true;
}

NAMESPACE_SC_END
