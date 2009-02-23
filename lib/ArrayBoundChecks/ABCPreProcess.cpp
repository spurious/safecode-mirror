//===- ABCPreProcess.cpp - Array Bounds Checking (Omega) ---------------- --////
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass was an old hack in the LLVM 1.x days; it grabbed information from
// function passes and placed the information into global variables so that it
// could be used by ModulePass's later on.
//
// LLVM 2.x and latet allow a ModulePass to require a FunctionPass, so most of
// this functionality has been removed.  The loop induction variable code still
// remains, however.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "ABCPreProcess.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Instruction.h"
#include "llvm/Constants.h"

using namespace llvm;
using namespace ABC;

char ABCPreProcess::ID = 0;

IndVarMap indMap;

void ABCPreProcess::print(ostream &out, const Module * M) const {
  out << " Printing phi nodes which are induction variables ... \n";
  IndVarMap::iterator ivCurrent = indMap.begin(), ivEnd = indMap.end();
  for (; ivCurrent != ivEnd; ++ivCurrent) {
    out << ivCurrent->first;
  }
  out << " Printing induction variables done ... \n";

}

void ABCPreProcess::indVariables(Loop *L) {
  PHINode *PN = L->getCanonicalInductionVariable();
  Value *V = L->getTripCount();
  if (PN && V) {
    indMap[PN] = V;
  }
  for (Loop::iterator I = L->begin(), E= L->end(); I!= E; ++I) {
    indVariables(*I);
  }
}

bool ABCPreProcess::runOnFunction(Function &F) {
  LoopInfo &LI =  getAnalysis<LoopInfo>();

  for (LoopInfo::iterator I = LI.begin(), E = LI.end(); I != E; ++I) {
    Loop *L = *I;
    indVariables(L);
  }
  return false;
}

RegisterPass<ABCPreProcess> Y("abcpre",
                              "Array Bounds Checking Pre-process pass");

Pass *createABCPreProcessPass() { return new ABCPreProcess(); }
