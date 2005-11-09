//===- ArrayBoundCheck.cpp - ArrayBounds Checking (Omega)----------------===//
//
// requires piNodeinsertion pass before
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

IndVarMap indMap;
DominatorSet::DomSetMapType dsmt;
PostDominatorSet::DomSetMapType pdsmt;
PostDominanceFrontier::DomSetMapType pdfmt;

void ABCPreProcess::print(ostream &out) {
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
  pdf = &getAnalysis<PostDominanceFrontier>();
  //copy it to a global for later use by a module pass
  PostDominanceFrontier::iterator pdfmI = pdf->begin(), pdfmE = pdf->end();
  for (; pdfmI != pdfmE; ++pdfmI) {
    PostDominanceFrontier::DomSetType &dst = pdfmI->second;
    DominatorSet::DomSetType::iterator dstI = dst.begin(), dstE = dst.end();
    for (; dstI != dstE; ++ dstI) {
      //Could this be optimized with stl version of set copy?
      BasicBlock *bb = pdfmI->first;
      pdfmt[pdfmI->first].insert(*dstI);
    }
  }
  
  ds = &getAnalysis<DominatorSet>();
  //copy it to a global for later use by a module pass
  DominatorSet::iterator dsmI = ds->begin(), dsmE = ds->end();
  for (; dsmI != dsmE; ++dsmI) {
    DominatorSet::DomSetType &dst = dsmI->second;
    DominatorSet::DomSetType::iterator dstI = dst.begin(), dstE = dst.end();
    for (; dstI != dstE; ++ dstI) {
      //Could this be optimized with stl version of set copy?
      dsmt[dsmI->first].insert(*dstI);
    }
  }

  
  pds = &getAnalysis<PostDominatorSet>();
  //copy it to a global for later use by a module pass
  PostDominatorSet::iterator pdsmI = pds->begin(), pdsmE = pds->end();
  for (; dsmI != dsmE; ++dsmI) {
    DominatorSet::DomSetType &dst = pdsmI->second;
    DominatorSet::DomSetType::iterator dstI = dst.begin(), dstE = dst.end();
    for (; dstI != dstE; ++ dstI) {
      //Could this be optimized with stl version of set copy?
      pdsmt[pdsmI->first].insert(*dstI);
    }
  }


  
  for (LoopInfo::iterator I = LI.begin(), E = LI.end(); I != E; ++I) {
    Loop *L = *I;
    indVariables(L);
  }
  return false;
}

RegisterOpt<ABCPreProcess> Y("abcpre",
                              "Array Bounds Checking preprocess pass");

Pass *createABCPreProcessPass() { return new ABCPreProcess(); }
