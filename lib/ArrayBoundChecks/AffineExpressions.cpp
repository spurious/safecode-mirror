//===- Expressions.cpp - Expression Analysis Utilities ----------------------=//
//
// This file defines a package of expression analysis utilties:
//
//
//===----------------------------------------------------------------------===//

#include "AffineExpressions.h"
#include "llvm/SlotCalculator.h"
#include "llvm/DerivedTypes.h"
#include "Support/StringExtras.h"
#include "llvm/ConstantHandling.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/InstrTypes.h"
#include "llvm/iTerminators.h"
#include "llvm/iMemory.h"
#include "llvm/iOther.h"
#include "llvm/iPHINode.h"
#include "llvm/BasicBlock.h"
#include <iostream>
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/InductionVariable.h"

using namespace llvm;

LinearExpr::LinearExpr(const Value *Val, SlotCalculator &Tab) {
  if (Val) {
    vList = new VarList();
    cMap = new CoefficientMap();
    vsMap = new ValStringMap();
    if (const ConstantSInt *CPI = dyn_cast<ConstantSInt>(Val)) {
      offSet = CPI->getValue();
      exprTy = Linear;
      return;
    } else if (const ConstantUInt *CPI = dyn_cast<ConstantUInt>(Val)) {
      offSet = CPI->getValue();
      exprTy = Linear;
      return;
    }
    offSet = 0;
    vList->push_back(Val);
    string tempstr;
    if (Val->hasName()) {
      tempstr = makeNameProper(Val->getName());
    } else {
      int Slot = Tab.getSlot(Val); // slot number 
      if (Slot >= 0) {
	tempstr = "l_" + itostr(Slot) + "_" + utostr(Val->getType()->getUniqueID()); 
      } else {
	exprTy = Unknown;
	tempstr = "unknown";
	return;
      }
    }
    (*vsMap)[Val] = tempstr;
    (*cMap)[Val] = 1;
  }
  else exprTy =  Unknown;
}


void
LinearExpr::negate() {
  mulByConstant(-1);
}


void
LinearExpr::print(ostream &out) {

  if (exprTy != Unknown) {
    VarListIt vlj = vList->begin();
    out << offSet;
    for (; vlj != vList->end(); ++vlj) 
      out << " + " << (*cMap)[*vlj] << " * " << (*vsMap)[*vlj];
  } else out << "Unknown ";
}

void
LinearExpr::addLinearExpr(LinearExpr *E) {
  if (E->getExprType() == Unknown) {
    exprTy = Unknown;
    return;
  }
  offSet = E->getOffSet() + offSet;
  VarList* vl = E->getVarList();
  CoefficientMap* cm = E->getCMap();
  ValStringMap* vsm = E->getVSMap();
  
  VarListIt vli = vl->begin();
  bool matched;
  for (; vli !=  vl->end(); ++vli) {
    matched = false;
    VarListIt vlj = vList->begin();
    for (; vlj != vList->end(); ++vlj) {
      if (*vli == *vlj) {
	//matched the vars .. now add the coefficients.
	(*cMap)[*vli] =  (*cMap)[*vli] + (*cm)[*vlj];
	matched = true;
	break;
      }
    }
    if (!matched) {
    //didnt match any var....
      vList->push_back(*vli);
      (*cMap)[*vli] = (*cm)[*vli];
      (*vsMap)[*vli] = (*vsm)[*vli];
    }
  }
}

LinearExpr *
LinearExpr::mulLinearExpr(LinearExpr *E) {
  if ((exprTy == Unknown) || (E->getExprType() == Unknown)) {
    exprTy = Unknown;
    return this;
  }
  VarList* vl = E->getVarList();
  if ((vl->size() != 0) && (vList->size() != 0)) {
    exprTy = Unknown;
    return this;
  }
  if (vl->size() == 0) {
    //The one coming in is a constant
    mulByConstant(E->getOffSet());
    return this;
  } else {
    E->mulByConstant(offSet);
    return E;
  }
}

void
LinearExpr::mulByConstant(int E) {
  offSet = offSet * E;
  VarListIt vlj = vList->begin();
  for (; vlj != vList->end(); ++vlj) {
    (*cMap)[*vlj] = (*cMap)[*vlj] * E;
  }
}

void
Constraint::print(ostream &out) {
  out << var;
  out << rel;
  le->print(out);
}


void
ABCExprTree::print(ostream &out) {
  if (constraint != 0) {
    //    out << "printing the constraint \n";
    constraint->print(out);
  }
  else {
    if (logOp == "||")
      out << "((";
    left->print(out);
    if (logOp == "||")
      out << ") ";
    out << "\n" << logOp;
    if (logOp == "||")
      out << "(";
    right->print(out);
    if (logOp == "||")
      out << "))";
  }
}

