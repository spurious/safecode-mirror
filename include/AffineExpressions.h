//===- AffineExpressions.h  - Expression Analysis Utils ----------*- C++ -*--=//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines a package of expression analysis utilties.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_C_EXPRESSIONS_H
#define LLVM_C_EXPRESSIONS_H

#include <assert.h>
#include <map>
#include <list>
#include <string>
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/InstrTypes.h"
#include "llvm/Constants.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Mangler.h"
#include "llvm/DerivedTypes.h"

using namespace std;
namespace llvm {


namespace cfg { class LoopInfo; }

typedef std::map<const PHINode *, Value *> IndVarMap;
typedef std::map<const Function *,BasicBlock *> ExitNodeMap;
typedef std::map<const Function *, PostDominanceFrontier *> PostDominanceFrontierMap;

typedef std::map<const Value*,int> CoefficientMap;
typedef std::map<const Value*,string> ValStringMap;
typedef std::list<const Value*> VarList;
typedef std::list<const Value*>::const_iterator VarListIt;
typedef std::list<const CallInst*> CallInstList;
typedef std::list<const CallInst*>::const_iterator CallInstListIt;
typedef std::map<Instruction*, bool> MemAccessInstListType;
typedef std::map<Instruction*, bool>::const_iterator  MemAccessInstListIt;

//
// Class: LinearExpr
//
// Description:
//  Represent an expression of the form CONST*VAR1 + CONST*VAR2 + ...
//
class LinearExpr {
    int offSet;
    VarList* vList;
    CoefficientMap* cMap;
    ValStringMap* vsMap;

  public:
    enum ExpressionType {
        Linear,        // Expression is linear
        Unknown        // Expression is some unknown type of expression
    } exprTy;

    inline int getOffSet() { return offSet; };
    inline void setOffSet(int offset) {  offSet = offset; };
    inline ExpressionType getExprType() { return exprTy; };
    inline VarList* getVarList() { return vList; };
    inline CoefficientMap* getCMap() { return cMap; };
    inline ValStringMap* getVSMap() { return vsMap; };

    // Create a linear expression
    LinearExpr(const Value *Val, Mangler *Mang);
    void negate();
    void addLinearExpr(LinearExpr *);
    LinearExpr * mulLinearExpr(LinearExpr *);
    void mulByConstant(int);
    void print(ostream &out);
    void printOmegaSymbols(ostream &out);
};

//
// Class: Constraint
//
// Description:
//  This class represents a constaint of the form <var> <rel> <expr> where:
//    <var>  : is a variable
//    <rel>  : is one of the following relations: <, >, <=, >=
//    <expr> : is a linear expression
//
class Constraint {
    string var;
    LinearExpr *le;

    // The relation: can be < ,  >, <=, >= for now
    string rel;

    // Flags whether the left-hand value is constant 
    bool leConstant_;

  public :
    Constraint(string v, LinearExpr *l, string r, bool leConstant = false ) {
      assert(l != 0 && "the rhs for this constraint is null");
      var = v;
      le = l;
      rel = r;
      leConstant_ = leConstant;
    }
    void print(ostream &out);
    void printOmegaSymbols(ostream &out);
};

//
// Class: ABCExprTree
//
// Description:
//  This class represents a set of relations that are connected together with
//  boolean AND and OR.  It represents the entire expression as a tree.  Each
//  node has a left and right subtree and either an AND or OR relation that
//  specified the relationship between the two subtrees.
//
class ABCExprTree {
    Constraint *constraint;
    ABCExprTree *right;
    ABCExprTree *left;
    string logOp; // can be && or || or
  public:
    ABCExprTree(Constraint *c) {
      constraint = c;
      left  = 0;
      right = 0;
      logOp = "&&";
    };

    ABCExprTree(ABCExprTree *l, ABCExprTree *r, string op) {
      constraint = 0;
      assert( l && " l is null \n");
      assert( r && " r is null \n");
      left = l;
      right = r;
      logOp = op;
    }
    void dump();
    void print(ostream &out);
    void printOmegaSymbols(ostream &out);
};

typedef std::map<const Value *, ABCExprTree *> InstConstraintMapType;

//
// Class: FuncLocalInfo
//
// Description:
// This holds the local information of a function.
//
class FuncLocalInfo {
    // Local cache for constraints
    InstConstraintMapType FuncLocalConstraints;

    // Storing all constraints which need proving 
    InstConstraintMapType FuncSafetyConstraints;

    // All array accesses in a function 
    MemAccessInstListType maiList;

    // This stores the Or of the arguments at
    // various call sites, so that can be computed only once for
    // different array accesses. 
    ABCExprTree *argConstraints;

  public :
    FuncLocalInfo() {
      argConstraints = 0;
    }

    inline void addMemAccessInst(Instruction *MAI, bool reqArg) {
      maiList[MAI] = reqArg;
    }

    inline void addLocalConstraint(const Value *v, ABCExprTree *aet) {
      FuncLocalConstraints[v] = aet;
    }

    inline bool inLocalConstraints(const Value *v) {
      return (FuncLocalConstraints.count(v) > 0);
    }

    inline ABCExprTree * getLocalConstraint(const Value *v) {
      if (FuncLocalConstraints.count(v)) return FuncLocalConstraints[v];
      else return 0;
    }

    inline void addSafetyConstraint(const Value *v, ABCExprTree *aet) {
      FuncSafetyConstraints[v] = aet;
    }

    inline ABCExprTree* getSafetyConstraint(const Value *v) {
      return (FuncSafetyConstraints[v]);
    }

    inline MemAccessInstListType getMemAccessInstList() {
      return maiList;
    }

    inline void addArgumentConstraints(ABCExprTree *aet) {
      argConstraints = aet;
    }

    inline ABCExprTree * getArgumentConstraints() {
      return argConstraints;
    }
};

//
// Function: makeNameProper()
//
// Description:
//  This function transforms a name to meet two requirements:
//    o) There are no invalid symbols.
//    o) The string length is 18 characters or less.
//  To do this, we replace symbols such as period, underscore, minus, etc.
//  with a letter followed by an underscore.
//
// Notes:
//  FIXME: This function always converts "in" to "in__1."  I am not sure why
//         this is.
//
static inline string
makeNameProper (string x) {
  string tmp;
  int len = 0;
  for (string::iterator sI = x.begin(), sEnd = x.end(); sI != sEnd; sI++) {
    if (len > 18) return tmp; //have to do something more meaningful
    len++;
    switch (*sI) {
      case '.': tmp += "d_"; len++;break;
      case ' ': tmp += "s_"; len++;break;
      case '-': tmp += "D_"; len++;break;
      case '_': tmp += "l_"; len++;break;
      default:  tmp += *sI;
    }
  }

  if (tmp == "in") return "in__1";
  return tmp;
}

#if 0
static string getValueNames(const Value *V, Mangler *Mang) {
  if (const Constant *CPI = dyn_cast<Constant>(V)) {
    if (const ConstantSInt *CPI = dyn_cast<ConstantSInt>(V)) {
      return itostr(CPI->getValue());
    } else if (const ConstantUInt *CPI = dyn_cast<ConstantUInt>(V)) {
      return utostr(CPI->getValue());
    }
  }
  if (V->hasName()) {
    return makeNameProper(V->getName());
  }
  else {
    return Mang->getValueName(V);
  }
};
#endif
}

#endif
