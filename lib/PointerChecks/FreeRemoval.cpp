//===-- FreeRemoval.cpp - EmbeC transformation that removes frees ------------//
// Implementation of FreeRemoval.h : an EmbeC pass
// 
// Some assumptions:
// * Correctness of pool allocation
// * Destroys at end of functions.
// Pool pointer aliasing assumptions
// -pool pointer copies via gep's are removed
// -no phinode takes two pool pointers because then they would be the same pool
// Result: If we look at pool pointer defs and look for their uses... we check 
// that their only uses are calls to pool_allocs, pool_frees and pool_destroys.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "FreeRemoval"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include "Support/PostOrderIterator.h"
#include "llvm/DerivedTypes.h"
#include "llvm/iOther.h"
#include "llvm/iMemory.h"
#include "llvm/Constants.h"
#include "llvm/Support/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "/home/vadve/kowshik/llvm/projects/poolalloc/include/poolalloc/PoolAllocate.h"
#include "Support/VectorExtras.h"
#include "llvm/Analysis/DataStructure.h"
#include "llvm/Analysis/DSGraph.h"
#include "Support/Debug.h"
#include <set>
#include <map>
#include <string>
using std::set;
using std::map;

using namespace llvm;

namespace {

  struct EmbeCFreeRemoval : public Pass {
    
    // The function representing 'poolmakeunfreeable'
    Function *PoolMakeUnfreeable;

    Function *PoolCheck;

    bool run(Module &M);
    
    static const std::string PoolI;
    static const std::string PoolA;
    static const std::string PoolF;
    static const std::string PoolD;
    static const std::string PoolMUF;
    static const std::string PoolCh;
    
    void checkPoolSSAVarUses(Function *F, Value *V, 
			     map<Value *, set<Instruction *> > &FuncAllocs, 
			     map<Value *, set<Instruction *> > &FuncFrees, 
			     map<Value *, set<Instruction *> > &FuncDestroy,
			     const CompleteBUDataStructures::ActualCalleesTy &AC);

    void propagateCollapsedInfo(Function *F, Value *V, 
				const CompleteBUDataStructures::ActualCalleesTy &AC);

    void addRuntimeChecks(Function *F, Function *Forig);
    
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // TODO: Check!
      AU.addRequired<PoolAllocate>();
      AU.addRequired<CallGraph>();
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      // AU.setPreservesAll();
    }
    
  private:
    
    Module *CurModule;

    TDDataStructures *TDDS;
    CompleteBUDataStructures *BUDS;
    PoolAllocate *PoolInfo;
    
    bool moduleChanged;
    bool hasError;
    
    // The following maps are only for pool pointers that escape a function.
    // Associates function with set of pools that are freed or alloc'ed using 
    // pool_free or pool_alloc but not destroyed within the function.
    // These have to be pool pointer arguments to the function
    map<Function *, set<Value *> > FuncFreedPools;
    map<Function *, set<Value *> > FuncAllocedPools;
    map<Function *, set<Value *> > FuncDestroyedPools;

    // Maps from a function to a set of Pool pointers and DSNodes from the 
    // original function corresponding to collapsed pools.
    map <Function *, set<Value *> > CollapsedPoolPtrs;

  };
  
  const std::string EmbeCFreeRemoval::PoolI = "poolinit";
  const std::string EmbeCFreeRemoval::PoolA = "poolalloc";
  const std::string EmbeCFreeRemoval::PoolF = "poolfree";
  const std::string EmbeCFreeRemoval::PoolD = "pooldestroy";
  const std::string EmbeCFreeRemoval::PoolMUF = "poolmakeunfreeable";
  const std::string EmbeCFreeRemoval::PoolCh = "poolcheck";

  RegisterOpt<EmbeCFreeRemoval> Y("EmbeC", "EmbeC pass that removes all frees and issues warnings if behaviour has changed");
  
}


// Check if SSA pool pointer variable V has uses other than alloc, free and 
// destroy
void EmbeCFreeRemoval::checkPoolSSAVarUses(Function *F, Value *V, 
			 map<Value *, set<Instruction *> > &FuncPoolAllocs,
			 map<Value *, set<Instruction *> > &FuncPoolFrees, 
			 map<Value *, set<Instruction *> > &FuncPoolDestroys,
			 const CompleteBUDataStructures::ActualCalleesTy &AC) {
  if (V->use_begin() != V->use_end()) {
    for (Value::use_iterator UI = V->use_begin(), UE = V->use_end();
	 UI != UE; ++UI) {
      // Check that the use is nothing except a call to pool_alloc, pool_free
      // or pool_destroy
      if (Instruction *I = dyn_cast<Instruction>(*UI)) {
	// For global pools, we need to check that only uses within the
	// function under consideration are checked.
	if (I->getParent()->getParent() != F)
	  continue;
      } else 
	continue;
      if (CallInst *CI = dyn_cast<CallInst>(*UI)) {
	if (Function *calledF = dyn_cast<Function>(CI->getOperand(0))) {
	  if (calledF == F) {
	    int operandNo;
	    for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	      if (CI->getOperand(i) == V) {
		operandNo = i;
		break;
	      }
	    
	    Value *formalParam;
	    int opi = 0;
	    for (Function::aiterator I = calledF->abegin(), 
		   E = calledF->aend();
		 I != E && opi < operandNo; ++I, ++opi)
	      if (opi == operandNo - 1) 
		formalParam = I;

	    if (formalParam == V)
	      ;
	    else {
	      std::cerr << "EmbeC: " << F->getName() 
		      << ": Recursion not supported for case classification\n";
	      continue;
	    }
	  }
	  if (!calledF->isExternal()) {
	    // the pool pointer is passed to the called function
	    
	    // Find the formal parameter corresponding to the parameter V
	    int operandNo;
	    for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	      if (CI->getOperand(i) == V) {
		operandNo = i;
		break;
	      }
	    
	    Value *formalParam;
	    int opi = 0;
	    for (Function::aiterator I = calledF->abegin(), 
		   E = calledF->aend();
		 I != E && opi < operandNo; ++I, ++opi)
	      if (opi == operandNo - 1) 
		formalParam = I;
	    
	    // if the called function has undestroyed frees in pool formalParam
	    if (FuncFreedPools[calledF].find(formalParam) != 
		FuncFreedPools[calledF].end() && 
		FuncDestroyedPools[calledF].find(formalParam) == 
		FuncDestroyedPools[calledF].end()) {
	      FuncPoolFrees[V].insert(cast<Instruction>(*UI));
	    }
	    // if the called function has undestroyed allocs in formalParam
	    if (FuncAllocedPools[calledF].find(formalParam) != 
		FuncAllocedPools[calledF].end()) {
	      FuncPoolAllocs[V].insert(cast<Instruction>(*UI));
	    }
	    
	    // if the called function has a destroy in formalParam
	    if (FuncDestroyedPools[calledF].find(formalParam) != 
		FuncDestroyedPools[calledF].end()) {
	      FuncPoolDestroys[V].insert(cast<Instruction>(*UI));
	    }
	  } else {
	    // external function
	    if (calledF->getName() == EmbeCFreeRemoval::PoolI) {
	      // Insert call to poolmakeunfreeable after every poolinit since 
	      // we do not free memory to the system for safety in all cases.
	      new CallInst(PoolMakeUnfreeable, make_vector(V, 0), "", 
			   CI->getNext());
	      moduleChanged = true;
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolA) {
	      FuncPoolAllocs[V].insert(cast<Instruction>(*UI));
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolF) {
	      FuncPoolFrees[V].insert(cast<Instruction>(*UI));
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolD) {
	      FuncPoolDestroys[V].insert(cast<Instruction>(*UI));
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolMUF) {
	      // Ignore
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolCh) {
	      // Ignore
	    } else {
	      hasError = true;
	      std::cerr << "EmbeC: " << F->getName() << ": Unrecognized pool variable use \n";
	    }
	  } 
	} else {
	  // indirect function call
	  std::pair<CompleteBUDataStructures::ActualCalleesTy::const_iterator, CompleteBUDataStructures::ActualCalleesTy::const_iterator> Callees = AC.equal_range(CI);

	  // Find the formal parameter corresponding to the parameter V
	  int operandNo;
	  for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	    if (CI->getOperand(i) == V)
	      operandNo = i;

	  for (CompleteBUDataStructures::ActualCalleesTy::const_iterator CalleesI = Callees.first; 
	       CalleesI != Callees.second; ++CalleesI) {
	    Function *calledF = CalleesI->second;
	    
	    PA::FuncInfo *PAFI = PoolInfo->getFunctionInfo(calledF);
	    
	    /*
	      if (PAFI->PoolArgFirst == PAFI->PoolArgLast ||
	      operandNo-1 < PAFI->PoolArgFirst ||
	      operandNo-1 >= PAFI->PoolArgLast)
	      continue;
	    */

	    Value *formalParam;
	    int opi = 0;
	    for (Function::aiterator I = calledF->abegin(), 
		   E = calledF->aend();
		 I != E && opi < operandNo; ++I, ++opi)
	      if (opi == operandNo-1) 
		formalParam = I;
	    
	    // if the called function has undestroyed frees in pool formalParam
	    if (FuncFreedPools[calledF].find(formalParam) != 
		FuncFreedPools[calledF].end() && 
		FuncDestroyedPools[calledF].find(formalParam) == 
		FuncDestroyedPools[calledF].end()) {
	      FuncPoolFrees[V].insert(cast<Instruction>(*UI));
	    }
	    // if the called function has undestroyed allocs in formalParam
	    if (FuncAllocedPools[calledF].find(formalParam) != 
		FuncAllocedPools[calledF].end()) {
	      FuncPoolAllocs[V].insert(cast<Instruction>(*UI));
	    }
	    
	    // if the called function has a destroy in formalParam
	    if (FuncDestroyedPools[calledF].find(formalParam) != 
		FuncDestroyedPools[calledF].end()) {
	      FuncPoolDestroys[V].insert(cast<Instruction>(*UI));
	    }
	    
	  }
	}
      } else {
	hasError = true;
	std::cerr << "EmbeC: " << F->getName() << ": Unrecognized pool variable use \n";
      }   
    }
  }
}

// Propagate that the pool V is a collapsed pool to each of the callees of F
// that have V as argument
void EmbeCFreeRemoval::propagateCollapsedInfo(Function *F, Value *V, 
					      const CompleteBUDataStructures::ActualCalleesTy &AC) {
  for (Value::use_iterator UI = V->use_begin(), 
	 UE = V->use_end(); UI != UE; ++UI) {
    if (CallInst *CI = dyn_cast<CallInst>(*UI)) {
      if (Function *calledF = 
	  dyn_cast<Function>(CI->getOperand(0))) {
	if (calledF == F) {
	  // Quick check for the common case
	  // Find the formal parameter corresponding to the parameter V
	  int operandNo;
	  for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	    if (CI->getOperand(i) == V) {
	      operandNo = i;
	      break;
	    }
	  
	  Value *formalParam;
	  int opi = 0;
	  for (Function::aiterator I = calledF->abegin(), 
		 E = calledF->aend();
	       I != E && opi < operandNo; ++I, ++opi)
	    if (opi == operandNo - 1) 
	      formalParam = I; 
	  if (formalParam == V) {
	    // This is the common case.
	  }
	  else {
	    std::cerr << "EmbeC: " << F->getName() 
		    << ": Recursion not supported\n";
	    continue;
	  }
	}
	if (!calledF->isExternal()) {
	  // the pool pointer is passed to the called function
	  
	  // Find the formal parameter corresponding to the parameter V
	  int operandNo;
	  for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	    if (CI->getOperand(i) == V) {
	      operandNo = i;
	      break;
	    }
	  
	  Value *formalParam;
	  int opi = 0;
	  for (Function::aiterator I = calledF->abegin(), 
		 E = calledF->aend();
	       I != E && opi < operandNo; ++I, ++opi)
	    if (opi == operandNo - 1) 
	      formalParam = I;
	  
	  CollapsedPoolPtrs[calledF].insert(formalParam);
	} 
      } else {
	  // indirect function call
	std::pair<CompleteBUDataStructures::ActualCalleesTy::const_iterator, CompleteBUDataStructures::ActualCalleesTy::const_iterator> Callees = AC.equal_range(CI);
	
	// Find the formal parameter corresponding to the parameter V
	int operandNo;
	for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	  if (CI->getOperand(i) == V)
	    operandNo = i;
	
	for (CompleteBUDataStructures::ActualCalleesTy::const_iterator CalleesI = Callees.first; 
	     CalleesI != Callees.second; ++CalleesI) {
	  Function *calledF = CalleesI->second;
	  
	  PA::FuncInfo *PAFI = PoolInfo->getFunctionInfo(calledF);
	  
	  /*
	    if (PAFI->PoolArgFirst == PAFI->PoolArgLast ||
	    operandNo-1 < PAFI->PoolArgFirst ||
	    operandNo-1 >= PAFI->PoolArgLast)
	    continue;
	  */
	  
	  Value *formalParam;
	  int opi = 0;
	  for (Function::aiterator I = calledF->abegin(), 
		 E = calledF->aend();
	       I != E && opi < operandNo; ++I, ++opi)
	    if (opi == operandNo-1) 
	      formalParam = I;
	  CollapsedPoolPtrs[calledF].insert(formalParam);
	}
      }
    } 
  }
}


// Returns true if BB1 follows BB2 in some path in F
static bool followsBlock(BasicBlock *BB1, BasicBlock *BB2, Function *F,
			 set<BasicBlock *> visitedBlocks) {
  for (succ_iterator BBSI = succ_begin(BB2), BBSE = 
	 succ_end(BB2); BBSI != BBSE; ++BBSI) {
    if (visitedBlocks.find(*BBSI) == visitedBlocks.end())
      if (*BBSI == BB1)
	return true;
      else {
	visitedBlocks.insert(*BBSI);
	if (followsBlock(BB1, *BBSI, F, visitedBlocks))
	  return true;
      }
  }

  return false;
}

// Checks if Inst1 follows Inst2 in any path in the function F.
static bool followsInst(Instruction *Inst1, Instruction *Inst2, Function *F) {
  if (Inst1->getParent() == Inst2->getParent()) {
    for (BasicBlock::iterator BBI = Inst2, BBE = Inst2->getParent()->end();
	 BBI != BBE; ++BBI)
      if (Inst1 == &(*BBI))
	return true;
  }
  set<BasicBlock *> visitedBlocks;
  return followsBlock(Inst1->getParent(), Inst2->getParent(), F, 
		      visitedBlocks);
}

static void printSets(set<Value *> &FuncPoolPtrs,
		      map<Value *, set<Instruction *> > &FuncPoolFrees,
		      map<Value *, set<Instruction *> > &FuncPoolAllocs) {
  for (set<Value *>::iterator I = FuncPoolPtrs.begin(), E = FuncPoolPtrs.end();
       I != E; ++I) {
    std::cerr << "Pool Variable: " << *I << "\n";
    if (FuncPoolFrees[*I].size()) {
      std::cerr << "Frees :" << "\n";
      for (set<Instruction *>::iterator FreeI = 
	     FuncPoolFrees[*I].begin(), FreeE = FuncPoolFrees[*I].end();
	   FreeI != FreeE; ++FreeI) {
	CallInst *CInst = dyn_cast<CallInst>(*FreeI);
	Function *CIF = dyn_cast<Function>(CInst->getOperand(0));
	std::cerr << CIF->getName() << "\n";
      }
    }
    if (FuncPoolAllocs[*I].size()) {
      std::cerr << "Allocs :" << "\n";
      for (set<Instruction *>::iterator AllocI = 
	     FuncPoolAllocs[*I].begin(), AllocE = FuncPoolAllocs[*I].end();
	   AllocI != AllocE; ++AllocI) {	
	CallInst *CInst = dyn_cast<CallInst>(*AllocI);
	Function *CIF = dyn_cast<Function>(CInst->getOperand(0));
	std::cerr << CIF->getName() << "\n";	
      }
    }
  }
}

// Insert runtime checks. Called on the functions in the existing program
void EmbeCFreeRemoval::addRuntimeChecks(Function *F, Function *Forig) {

  bool isClonedFunc;
  PA::FuncInfo* PAFI = PoolInfo->getFunctionInfo(F);

  if (PoolInfo->getFuncInfo(*F))
    isClonedFunc = false;
  else
    isClonedFunc = true;
  
  DSGraph& oldG = BUDS->getDSGraph(*Forig);
  
  // For each scalar pointer in the original function
  if (!PAFI->PoolDescriptors.empty()) {
    for (DSGraph::ScalarMapTy::iterator SMI = oldG.getScalarMap().begin(), 
	   SME = oldG.getScalarMap().end(); SMI != SME; ++SMI) {
      DSNodeHandle &GH = SMI->second;
      DSNode *DSN = GH.getNode();
      if (!DSN) 
	continue;
      if (DSN->isUnknownNode()) {
	// Report an error if we see loads or stores on SMI->first
	Value *NewPtr = SMI->first;
	if (isClonedFunc)
	  NewPtr = PAFI->ValueMap[SMI->first];
	if (!NewPtr)
	  continue;
	for (Value::use_iterator UI = NewPtr->use_begin(), 
	       UE = NewPtr->use_end(); UI != UE; ++UI) {
	  if (StoreInst *StI = dyn_cast<StoreInst>(*UI)) {
	    if (StI->getOperand(1) == NewPtr) {
	      std::cerr << 
		"EmbeC: In function " << F->getName() << ": Presence of an unknown node can invalidate pool allocation\n";
	      break;
	    }
	  } else if (LoadInst *LI = dyn_cast<LoadInst>(*UI)) {
	    std::cerr << 
	      "EmbeC: In function " << F->getName() << ": Presence of an unknown node can invalidate pool allocation\n";
	    break;
	  }
	}
	/*
        Value *NewPtr = SMI->first;
	if (isClonedFunc)
	  NewPtr = PAFI->ValueMap[SMI->first];
	if (!NewPtr)
	  continue;
	for (Value::use_iterator UI = NewPtr->use_begin(), 
	       UE = NewPtr->use_end(); UI != UE; ++UI) {
	  if (StoreInst *StI = dyn_cast<StoreInst>(*UI)) {
	    if (StI->getOperand(1) == NewPtr) {
	      moduleChanged = true;
	      CastInst *CastI = 
		new CastInst(StI->getOperand(1), 
			     PointerType::get(Type::SByteTy), "casted", StI);
	      new CallInst(PoolCheck, 
			   make_vector(PAFI->PoolDescriptors[DSN], CastI, 0),
			   "", StI);
	    }
	  } else if (LoadInst *LI = dyn_cast<LoadInst>(*UI)) {
	    moduleChanged = true;
	    CastInst *CastI = 
	      new CastInst(LI->getOperand(0), 
			   PointerType::get(Type::SByteTy), "casted", StI);
	    new CallInst(PoolCheck, 
			 make_vector(PAFI->PoolDescriptors[DSN], CastI, 0),
			 "", LI);
	  }
	}
	*/
      } 
      if (PAFI->PoolDescriptors.count(DSN)) {
	// If the node pointed to, corresponds to a collapsed pool 
	if (CollapsedPoolPtrs[F].find(PAFI->PoolDescriptors[DSN]) !=
	    CollapsedPoolPtrs[F].end()) {
	  // find uses of the coresponding new pointer
	  Value *NewPtr = SMI->first;
	  if (isClonedFunc)
	    NewPtr = PAFI->ValueMap[SMI->first];
	  if (!NewPtr)
	    continue;
	  for (Value::use_iterator UI = NewPtr->use_begin(), 
		 UE = NewPtr->use_end(); UI != UE; ++UI) {
	    // If the use is the 2nd operand of store, insert a runtime check
	    if (StoreInst *StI = dyn_cast<StoreInst>(*UI)) {
	      if (StI->getOperand(1) == NewPtr) {
		moduleChanged = true;
		CastInst *CastI = 
		  new CastInst(StI->getOperand(1), 
			       PointerType::get(Type::SByteTy), "casted", StI);
		new CallInst(PoolCheck, 
			     make_vector(PAFI->PoolDescriptors[DSN], CastI, 0),
			     "", StI);
	      }
	    } else if (CallInst *CallI = dyn_cast<CallInst>(*UI)) {
	      // If this is a function pointer read from a collapsed node,
	      // reject the code
	      if (CallI->getOperand(0) == NewPtr) {
		std::cerr << 
		  "EmbeC: Error - Function pointer read from collapsed node\n";
	      }
	    }
	  }
	}
      }
    }
  }
}

bool EmbeCFreeRemoval::run(Module &M) {
  CurModule = &M;
  moduleChanged = false;
  hasError = false;

  // Insert prototypes in the module
  // NB: This has to be in sync with the types in PoolAllocate.cpp
  const Type *VoidPtrTy = PointerType::get(Type::SByteTy);

  const Type *PoolDescType = 
    StructType::get(make_vector<const Type*>(VoidPtrTy, VoidPtrTy, 
					     Type::UIntTy, Type::UIntTy, 0));
  //    StructType::get(make_vector<const Type*>(VoidPtrTy, VoidPtrTy,
  //					     Type::UIntTy, Type::UIntTy, 0));
  const PointerType *PoolDescPtr = PointerType::get(PoolDescType);
  FunctionType *PoolMakeUnfreeableTy = 
    FunctionType::get(Type::VoidTy,
		      make_vector<const Type*>(PoolDescPtr, 0),
		      false);

  FunctionType *PoolCheckTy = 
    FunctionType::get(Type::VoidTy,
		      make_vector<const Type*>(PoolDescPtr, VoidPtrTy, 0),
		      false);
  
  PoolMakeUnfreeable = CurModule->getOrInsertFunction("poolmakeunfreeable", 
						      PoolMakeUnfreeableTy);
  
  PoolCheck = CurModule->getOrInsertFunction("poolcheck", PoolCheckTy);
  
  moduleChanged = true;

  Function *mainF = M.getMainFunction();
  
  if (!mainF) {
    hasError = true;
    std::cerr << "EmbeC: Function main required\n";
    return false;
  }

  // Bottom up on the call graph
  // TODO: Take care of recursion/mutual recursion
  PoolInfo = &getAnalysis<PoolAllocate>();
  CallGraph &CG = getAnalysis<CallGraph>();

  BUDS = &getAnalysis<CompleteBUDataStructures>();
  TDDS = &getAnalysis<TDDataStructures>();
  // For each function, all its pool SSA variables including its arguments
  map<Function *, set<Value *> > FuncPoolPtrs;

  for (po_iterator<CallGraph*> CGI = po_begin(&CG), 
	 CGE = po_end(&CG); CGI != CGE; ++CGI) {
    
    Function *F = (*CGI)->getFunction();
    
    // Ignore nodes representing external functions in the call graph
    if (!F)
      continue;
    
    // Pool SSA variables that are used in allocs, destroy and free or calls 
    // to functions that escaped allocs, destroys and frees respectively.
    map<Value *, set<Instruction *> > FuncPoolAllocs, FuncPoolFrees, 
      FuncPoolDestroys;
    
    // Traverse the function finding poolfrees and calls to functions that
    // have poolfrees without pooldestroys on all paths in that function.
    
    if (!F->isExternal()) {
      // For each pool pointer def check its uses and ensure that there are 
      // no uses other than the pool_alloc, pool_free or pool_destroy calls
      
      PA::FuncInfo* PAFI = PoolInfo->getFunctionInfo(F);
      
      // If the function has no pool pointers (args or SSA), ignore the 
      // function.
      if (!PAFI)
	continue;
      
      if (PAFI->Clone && PAFI->Clone != F)
	continue;

      const CompleteBUDataStructures::ActualCalleesTy &AC = 
	BUDS->getActualCallees();
      

      if (!PAFI->PoolDescriptors.empty()) {
	for (std::map<DSNode*, Value*>::iterator PoolDI = 
	       PAFI->PoolDescriptors.begin(), PoolDE = 
	       PAFI->PoolDescriptors.end(); PoolDI != PoolDE; ++PoolDI) {
	  checkPoolSSAVarUses(F, PoolDI->second, FuncPoolAllocs, 
	  		      FuncPoolFrees, FuncPoolDestroys, AC);
	  FuncPoolPtrs[F].insert(PoolDI->second);
	}
      }
      else
	continue;

      /*
      if (F->getName() == "main") {
	std::cerr << "In Function " << F->getName() << "\n";
	printSets(FuncPoolPtrs[F], FuncPoolFrees, FuncPoolAllocs);
      }
      */

      /*
      // Implementing the global analysis that checks that
      // - For each poolfree, if there is a poolallocate on another pool
      //   before a pooldestroy on any path, then this is a case 3 pool.
      //   If there is a poolallocate on the same pool before a pooldestroy
      //   on any path, then it is a case 2 pool.
     
      // Do a local analysis on these and update the pool variables that escape
      // the function (poolfrees without pooldestroys, poolallocs)
      // For each instruction in the list of free/calls to free, do
      // the following: go down to the bottom of the function looking for 
      // allocs till you see destroy. If you don't see destroy on some path, 
      // update escape list.
      // Update escape list with allocs without destroys for arguments
      // as well as arguments that are destroyed.
      // TODO: Modify implementation so you are not checking that there is no
      // alloc to other pools follows a free in the function, but to see that 
      // there is a destroy on pool between a free and an alloc to another pool
      // Current Assumption: destroys are at the end of a function.
      if (!FuncPoolFrees.empty()) {
	std::cerr << "In Function " << F->getName() << "\n";
	for (map<Value *, set<Instruction *> >::iterator ValueI = 
	       FuncPoolFrees.begin(), ValueE = FuncPoolFrees.end();
	     ValueI != ValueE; ++ValueI) {
	  bool case3found = false, case2found = false;
	  for (set<Instruction *>::iterator FreeInstI = 
		 (*ValueI).second.begin(), 
		 FreeInstE = (*ValueI).second.end();
	       FreeInstI != FreeInstE && !case3found; ++FreeInstI) {
	    // For each free instruction or call to a function which escapes
	    // a free in pool (*ValueI).first
	    for (set<Value *>::iterator PoolPtrsI = FuncPoolPtrs[F].begin(), 
		   PoolPtrsE = FuncPoolPtrs[F].end(); 
		 PoolPtrsI != PoolPtrsE && !case3found; 
		 ++PoolPtrsI) {
	      // For each pool pointer other than the one in the free 
	      // instruction under consideration, check that allocs to it
	      // don't follow the free on any path.
	      if (*PoolPtrsI !=  (*ValueI).first) {
		map<Value *, set<Instruction *> >::iterator AllocSet= 
		  FuncPoolAllocs.find(*PoolPtrsI);
		if (AllocSet != FuncPoolAllocs.end())
		  for (set<Instruction *>::iterator AllocInstI = 
			 (*AllocSet).second.begin(), 
			 AllocInstE = (*AllocSet).second.end();
		       AllocInstI != AllocInstE && !case3found; ++AllocInstI)
		    if (followsInst(*AllocInstI, *FreeInstI, F) &&
			*AllocInstI != *FreeInstI)
		      case3found = true;
	      } else {
		map<Value *, set<Instruction *> >::iterator AllocSet= 
		  FuncPoolAllocs.find(*PoolPtrsI);
		if (AllocSet != FuncPoolAllocs.end())
		  for (set<Instruction *>::iterator AllocInstI = 
			 (*AllocSet).second.begin(), 
			 AllocInstE = (*AllocSet).second.end();
		       AllocInstI != AllocInstE && !case3found; ++AllocInstI)
		    if (followsInst(*AllocInstI, *FreeInstI, F) &&
			*AllocInstI != *FreeInstI)
		      case2found = true;
	      }
	    }
	  }
	  if (case3found && case2found)
	    std::cerr << (*ValueI).first->getName() 
		      << ": Case 2 and 3 detected\n";
	  else if (case3found)
	    std::cerr << (*ValueI).first->getName() 
		      << ": Case 3 detected\n";
	  else if (case2found)
	    std::cerr << (*ValueI).first->getName() 
		      << ": Case 2 detected\n";
	  else
	    std::cerr << (*ValueI).first->getName() 
		      << ": Case 1 detected\n";
	}
      }
      */
      
      // Assumption: if we have pool_destroy on a pool in a function, then it
      // is on all exit paths of the function
      // TODO: correct later. 
      // Therefore, all pool ptr arguments that have frees but no destroys
      // escape the function. Similarly all pool ptr arguments that have
      // allocs but no destroys escape the function      
      for (set<Value *>::iterator PoolPtrsI = FuncPoolPtrs[F].begin(), 
	     PoolPtrsE = FuncPoolPtrs[F].end(); PoolPtrsI != PoolPtrsE; 
	   ++PoolPtrsI) {
	if (isa<Argument>(*PoolPtrsI)) {
	  // Only for pool pointers that are arguments
	  if (FuncPoolFrees.find(*PoolPtrsI) != FuncPoolFrees.end() &&
	      FuncPoolFrees[*PoolPtrsI].size())
	    FuncFreedPools[F].insert(*PoolPtrsI);
	  if (FuncPoolAllocs.find(*PoolPtrsI) != FuncPoolAllocs.end() &&
	      FuncPoolAllocs[*PoolPtrsI].size())
	    FuncAllocedPools[F].insert(*PoolPtrsI);
	  if (FuncPoolDestroys.find(*PoolPtrsI) != FuncPoolDestroys.end() &&
	      FuncPoolDestroys[*PoolPtrsI].size()) {
	    FuncDestroyedPools[F].insert(*PoolPtrsI);
	  }
	} 
      }
      
      // TODO
      // For each function, check that the frees in the function are case 1 
      // i.e. there are no mallocs between the free and its corresponding 
      // pool_destroy and then remove the pool free call.
    }
  }
    
  // Now, traverse the call graph top-down, updating information about pool 
  // pointers that may be collapsed and inserting runtime checks
  ReversePostOrderTraversal<CallGraph*> RPOT(&CG); 
  for (ReversePostOrderTraversal<CallGraph*>::rpo_iterator CGI = RPOT.begin(),
	 CGE = RPOT.end(); CGI != CGE; ++CGI) {
    Function *F = (*CGI)->getFunction();
    
    if (!F)
      continue;
    
    // Ignore nodes representing external functions in the call graph
    if (!F->isExternal()) {
      // For each pool pointer def check its uses and ensure that there are 
      // no uses other than the pool_alloc, pool_free or pool_destroy calls
      
      PA::FuncInfo* PAFI = PoolInfo->getFunctionInfo(F);

      if (!PAFI)
	continue;

      if (PAFI->Clone && PAFI->Clone != F)
	continue;

      Function *Forig;
      if (PAFI->Clone) {
	for(Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI)
	  if (PoolInfo->getFuncInfo(*MI))
	    if (PoolInfo->getFuncInfo(*MI) == PAFI) {
	      Forig = &*MI;
	      break;
	    }
      } else
	Forig = F;

      const CompleteBUDataStructures::ActualCalleesTy &AC = 
	BUDS->getActualCallees();

      if (FuncPoolPtrs.count(F)) {
	for (set<Value *>::iterator PDI = FuncPoolPtrs[F].begin(), 
	       PDE = FuncPoolPtrs[F].end(); PDI != PDE; ++PDI) {
	  if (isa<Argument>(*PDI)) {
	    if (CollapsedPoolPtrs[F].find(*PDI) != CollapsedPoolPtrs[F].end())
	      propagateCollapsedInfo(F, *PDI, AC);
	  } else {
	    // This pool is poolinit'ed in this function or is a global pool
	    DSNode *PDINode;
	    
	    for (std::map<DSNode*, Value*>::iterator PDMI = 
		   PAFI->PoolDescriptors.begin(), 
		   PDME = PAFI->PoolDescriptors.end(); PDMI != PDME; ++PDMI)
	      if (PDMI->second == *PDI) {
		PDINode = PDMI->first;
		break;
	      }
	    
	    if (PDINode->isNodeCompletelyFolded()) {
	      CollapsedPoolPtrs[F].insert(*PDI);
	      
	      for (unsigned i = 0 ; i < PDINode->getNumLinks(); ++i)
		if (!PDINode->getLink(i).getNode()->isNodeCompletelyFolded()) {
		  std::cerr << "EmbeC : In function " << F->getName() 
			    << ":Collapsed node pointing to non-collapsed node\n";
		  break;
		}

	      // Propagate this information to all the callees only if this
	      // is not a global pool
	      if (!isa<GlobalVariable>(*PDI))
		propagateCollapsedInfo(F, *PDI, AC);
	    }
	  }
	  
	}
	// At this point, we know all the collapsed pools in this function
	// Add run-time checks before all stores to pointers pointing to 
	// collapsed pools
	addRuntimeChecks(F, Forig);	
      }
    }
  }
  return moduleChanged;
  
}

Pass *createEmbeCFreeRemovalPass() { return new EmbeCFreeRemoval(); }
