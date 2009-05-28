//===- ArrayBoundsCheck.h ---------------------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass implements a static array bounds checking analysis pass.  It
// assumes that the ABCPreprocess pass is run before.
//
//===----------------------------------------------------------------------===//

#ifndef ARRAY_BOUNDS_CHECK_H_
#define ARRAY_BOUNDS_CHECK_H_

#include "safecode/SAFECode.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Instruction.h"
#include "llvm/Function.h"
#include "llvm/Pass.h"
#include "AffineExpressions.h"
#include "BottomUpCallGraph.h"

#include <map>
#include <set>

using namespace llvm;

NAMESPACE_SC_BEGIN

/// This class defines the interface of array bounds checking.
class ArrayBoundsCheckGroup {
public:
  static char ID;
  /// Determine whether a particular GEP instruction is always safe of not.
  virtual bool isGEPSafe(GetElementPtrInst * GEP) { return false; }
  virtual ~ArrayBoundsCheckGroup();
};

/// This is the dummy version of array bounds checking. It simply assumes that
/// every GEP instruction is unsafe.
class ArrayBoundsCheckDummy : public ArrayBoundsCheckGroup, public ImmutablePass {
public:
  static char ID;
  ArrayBoundsCheckDummy() : ImmutablePass((intptr_t) &ID) {}
};

/// ArrayBoundsCheckLocal - It tries to prove a GEP is safe only based on local
/// information, that is, the size of global variables and the size of objects
/// being allocated inside a function.
class ArrayBoundsCheckLocal : public ArrayBoundsCheckGroup, public FunctionPass {
public:
  static char ID;
  ArrayBoundsCheckLocal() : FunctionPass((intptr_t) &ID) {}
  virtual bool isGEPSafe(GetElementPtrInst * GEP);
  virtual void getAnalysisUsage(AnalysisUsage & AU) const {
    AU.addRequired<TargetData>();
    AU.setPreservesAll();  
  }
  virtual bool runOnFunction(Function & F);
private:
  TargetData * TD;
  int getObjectSize(Value * V);
  bool isConstantIndexGEP(GetElementPtrInst * GEP);
};


struct ArrayBoundsCheck : public ArrayBoundsCheckGroup, public ModulePass {
  public :
    static char ID;
    ArrayBoundsCheck () : ModulePass ((intptr_t) &ID) {}
    const char *getPassName() const { return "Array Bounds Check"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.addRequired<EQTDDataStructures>();
      AU.addRequired<BottomUpCallGraph>();
      AU.addRequired<DominatorTree>();
      AU.addRequired<PostDominatorTree>();
      AU.addRequired<PostDominanceFrontier>();
      AU.setPreservesAll();
    }

    void releaseMemory() {
      //
      // Delete all of the sets used to track unsafe GEPs.
      //
      std::map<BasicBlock *,std::set<Instruction*>*>::iterator i;
      for (i = UnsafeGetElemPtrs.begin(); i != UnsafeGetElemPtrs.end(); ++i) {
        delete i->second;
      }

      //
      // Clear the map.
      //
      UnsafeGetElemPtrs.clear();
    }

    std::map<BasicBlock *,std::set<Instruction*>*> UnsafeGetElemPtrs;

  
    virtual bool isGEPSafe(GetElementPtrInst * GEP) { 
      BasicBlock * BB = GEP->getParent();
      return UnsafeGetElemPtrs[BB]->count(GEP) == 0;
    }
  
  private :
    // Referenced passes
    EQTDDataStructures *cbudsPass;
    BottomUpCallGraph *buCG;

    typedef std::map<const Function *,FuncLocalInfo*> InfoMap;
    typedef std::map<Function*, int> FuncIntMap;

    DominatorTree * domTree;
    PostDominatorTree * postdomTree;
    PostDominanceFrontier * postdomFrontier;

    std::set<Instruction*> UnsafeCalls;

    // This is required for getting the names/unique identifiers for variables.
    OmegaMangler *Mang;

    // for storing local information about a function
    InfoMap fMap; 

    // Known Func Database
    std::set<string> KnownFuncDB;
    
    // for storing info about the functions which are already proven safe
    FuncIntMap provenSafe;

    // For storing what control dependent blocks are already dealt with for the
    // current array access
    std::set<BasicBlock *> DoneList;

    // Initializes the KnownFuncDB
    void initialize();
    
    void outputDeclsForOmega(Module &M);

    // Interface for collecting constraints for different array access in a
    // function
    void collectSafetyConstraints(Function &F);

    // This function collects from the branch which controls the current block
    // the Successor tells the path 
    void addBranchConstraints (BranchInst *BI,
                               BasicBlock *Succ,
                               ABCExprTree **rootp);

    // This method adds constraints for known trusted functions
    ABCExprTree* addConstraintsForKnownFunctions(Function *kf, CallInst *CI);
    
    // Mark an instruction as an unsafe GEP instruction
    void MarkGEPUnsafe (Instruction * GEP) {
      // Pointer to set of unsafe GEPs
      std::set<Instruction*> * UnsafeGEPs;

      if (!(UnsafeGetElemPtrs[GEP->getParent()]))
        UnsafeGetElemPtrs[GEP->getParent()] = new std::set<Instruction*>;
      UnsafeGEPs = UnsafeGetElemPtrs[GEP->getParent()];
      UnsafeGEPs->insert(GEP);
    }

    // Interface for getting constraints for a particular value
    void getConstraintsInternal( Value *v, ABCExprTree **rootp);
    void getConstraints( Value *v, ABCExprTree **rootp);

    // Adds all the conditions on which the currentblock is control dependent
    // on.
    void addControlDependentConditions(BasicBlock *curBB, ABCExprTree **rootp); 

    // Gives the return value constraints interms of its arguments 
    ABCExprTree* getReturnValueConstraints(Function *f);
    void getConstraintsAtCallSite(CallInst *CI,ABCExprTree **rootp);
    void addFormalToActual(Function *f, CallInst *CI, ABCExprTree **rootp);

    // Checks if the function is safe (produces output for omega consumption)
    void checkSafety(Function &F);

    // Get the constraints on the arguments
    // This goes and looks at all call sites and ors the corresponding
    // constraints
    ABCExprTree* getArgumentConstraints(Function &F);

    // for simplifying the constraints 
    LinearExpr* SimplifyExpression( Value *Expr, ABCExprTree **rootp);

    string getValueName(const Value *V);
    void generateArrayTypeConstraintsGlobal (string var,
                                             const ArrayType *T,
                                             ABCExprTree **rootp,
                                             unsigned int numElem);
    void generateArrayTypeConstraints (string var,
                                       const ArrayType *T,
                                       ABCExprTree **rootp);
    void printarraytype(string var,const ArrayType  *T);
    void printSymbolicStandardArguments(const Module *M, ostream & out);
    void printStandardArguments(const Module *M, ostream & out);
    void Omega(Instruction *maI, ABCExprTree *root );
  };

NAMESPACE_SC_END

#endif
