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

#include "dsa/CStdLib.h"
#include "safecode/CompleteChecks.h"

#include "llvm/ADT/Statistic.h"

#include <stdint.h>

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
// Function: makeCStdLibCallsComplete()
//
// Description:
//  Fills in completeness information for all calls of a given CStdLib function
//  assumed to be of the form:
//
//   pool_X(POOL *p1, ..., POOL *pN, void *a1, ..., void *aN, ..., uint8_t c);
//
//  Specifically, this function assumes that there are as many pointer arguments
//  to check as there are initial pool arguments, and the pointer arguments
//  follow the pool arguments in corrsponding order. Also, it is assumed that
//  the final argument to the function is a byte sized bit vector.
//
//  This function fills in this final byte with a constant value whose ith
//  bit is set exactly when the ith pointer argument is complete.
//
// Inputs:
//
//  F            - A pointer to the CStdLib function appearing in the module
//                 (non-null).
//  PoolArgs     - The number of initial pool arguments for which a
//                 corresponding pointer value requires a completeness check
//                 (required to be at most 8).
//
void
CompleteChecks::makeCStdLibCallsComplete(Function *F, unsigned PoolArgs) {
  assert(F != 0 && "Null function argument!");

  assert(PoolArgs <= 8 && \
    "Only up to 8 arguments are supported by CStdLib completeness checks!");

  Value::use_iterator U = F->use_begin();
  Value::use_iterator E = F->use_end();

  //
  // Hold the call instructions that need changing.
  //
  typedef std::pair<CallInst *, uint8_t> VectorReplacement;
  std::set<VectorReplacement> callsToChange;

  const Type *int8ty = Type::getInt8Ty(F->getContext());
  const FunctionType *F_type = F->getFunctionType();

  //
  // Verify the type of the function is as expected.
  //
  // There should be as many pointer parameters to check for completeness
  // as there are pool parameters. The last parameter should be a byte.
  //
  assert(F_type->getNumParams() >= PoolArgs * 2 && \
    "Not enough arguments to transformed CStdLib function call!");
  for (unsigned arg = PoolArgs; arg < PoolArgs * 2; ++arg)
    assert(isa<PointerType>(F_type->getParamType(arg)) && \
      "Expected pointer argument to function!");

  //
  // This is the position of the vector operand in the call.
  //
  unsigned vect_position = F_type->getNumParams();

  assert(F_type->getParamType(vect_position - 1) == int8ty && \
    "Last parameter to the function should be a byte!");

  //
  // Iterate over all calls of the function in the module, computing the
  // vectors for each call as it is found.
  //
  for (; U != E; ++U) {
    CallInst *CI;
    if ((CI = dyn_cast<CallInst>(*U)) && \
      CI->getCalledValue()->stripPointerCasts() == F) {

      uint8_t vector = 0x0;

      //
      // Get the parent function to which this instruction belongs.
      //
      Function *P = CI->getParent()->getParent();

      //
      // Iterate over the pointer arguments that need completeness checking
      // and build the completeness vector.
      //
      for (unsigned arg = 0; arg < PoolArgs; ++arg) {
        bool complete = true;
        //
        // Go past all the pool arguments to get the pointer to check.
        //
        Value *V = CI->getOperand(1 + PoolArgs + arg);

        //
        // Check for completeness of the pointer using DSA and 
        // set the bit in the vector accordingly.
        //
        DSNode *N;
        if ((N = getDSNodeHandle(V, P).getNode()) &&
              (N->isExternalNode() || N->isIncompleteNode() ||
               N->isUnknownNode()  || N->isIntToPtrNode()   ||
               N->isPtrToIntNode()) ) {
            complete = false;
        }

        if (complete)
          vector |= (1 << arg);
      }

      //
      // Add the instruction and vector to the set of instructions to change.
      //
      callsToChange.insert(VectorReplacement(CI, vector));
    }
  }


  //
  // Iterate over all call instructions that need changing, modifying the
  // final operand of the call to hold the bit vector value.
  //
  std::set<VectorReplacement>::iterator change = callsToChange.begin();
  std::set<VectorReplacement>::iterator change_end = callsToChange.end();

  while (change != change_end) {
    Constant *vect_value = ConstantInt::get(int8ty, change->second);
    change->first->setOperand(vect_position, vect_value);
    ++change;
  }

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

  //
  // Iterate over the CStdLib functions whose entries are known to DSA.
  // For each function call, do a completeness check on the given number of
  // pointer arguments and mark the completeness bit vector accordingly.
  //
  for (const CStdLibPoolArgCountEntry *entry = &CStdLibPoolArgCounts[0];
       entry->function != 0; ++entry) {
    Function *f = M.getFunction(entry->function);
    if (f != 0)
      makeCStdLibCallsComplete(f, entry->pool_argc);
  }

  return true;
}

NAMESPACE_SC_END
