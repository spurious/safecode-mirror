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

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include "Support/PostOrderIterator.h"
#include "llvm/DerivedTypes.h"
#include "llvm/iOther.h"
#include "llvm/Support/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "/home/vadve/dhurjati/llvm/projects/poolalloc/include/poolalloc/PoolAllocate.h"
#include "Support/VectorExtras.h"
#include "llvm/Analysis/DataStructure.h"
#include "llvm/Analysis/DSGraph.h"
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

    bool run(Module &M);
    
    static const std::string PoolI;
    static const std::string PoolA;
    static const std::string PoolF;
    static const std::string PoolD;
    
    void checkPoolSSAVarUses(Function *F, Value *V, 
			     map<Value *, set<Instruction *> > &FuncAllocs, 
			     map<Value *, set<Instruction *> > &FuncFrees, 
			     map<Value *, set<Instruction *> > &FuncDestroy);
    
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // TODO: Check!
      AU.setPreservesAll();
      AU.addRequired<PoolAllocate>();
      AU.addRequired<CallGraph>();
      AU.addRequired<TDDataStructures>();
    }
    
  private:
    
    Module *CurModule;

    TDDataStructures *TDDS;
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
    
  };
  
  const std::string EmbeCFreeRemoval::PoolI = "poolinit";
  const std::string EmbeCFreeRemoval::PoolA = "poolallocate";
  const std::string EmbeCFreeRemoval::PoolF = "poolfree";
  const std::string EmbeCFreeRemoval::PoolD = "pooldestroy";
  
  RegisterOpt<EmbeCFreeRemoval> Y("EmbeC", "EmbeC pass that removes all frees and issues warnings if behaviour has changed");
  
}


// Check if SSA pool pointer variable V has uses other than alloc, free and 
// destroy
void EmbeCFreeRemoval::checkPoolSSAVarUses(Function *F, Value *V, 
			 map<Value *, set<Instruction *> > &FuncPoolAllocs,
			 map<Value *, set<Instruction *> > &FuncPoolFrees, 
			 map<Value *, set<Instruction *> > &FuncPoolDestroys) {
  if (V->use_begin() != V->use_end()) {
    for (Value::use_iterator UI = V->use_begin(), UE = V->use_end();
	 UI != UE; ++UI) {
      // Check that the use is nothing except a call to pool_alloc, pool_free
      // or pool_destroy
      if (CallInst *CI = dyn_cast<CallInst>(*UI)) {
	if (Function *calledF = dyn_cast<Function>(CI->getOperand(0))) {
	  if (calledF == F) {
	    // Hack for recursion (not mutual)
	    continue;
	  }
	  if (!calledF->isExternal()) {
	    // the pool pointer is passed to the called function
	    
	    // Find the formal parameter corresponding to the parameter V
	    int operandNo;
	    for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	      if (CI->getOperand(i) == V)
		operandNo = i;
	    
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
	      // Also, insert prototype in the module

	      // NB: This has to be in sync with the types in PoolAllocate.cpp
	      const Type *VoidPtrTy = PointerType::get(Type::SByteTy);
	      // The type to allocate for a pool descriptor: { sbyte*, uint }
	      const Type *PoolDescType =
		StructType::get(make_vector<const Type*>(VoidPtrTy, 
							 Type::UIntTy, 0));
	      const PointerType *PoolDescPtr = PointerType::get(PoolDescType);
	      FunctionType *PoolMakeUnfreeableTy = 
		FunctionType::get(Type::VoidTy,
				  make_vector<const Type*>(PoolDescPtr, 0),
				  false);
	      PoolMakeUnfreeable = CurModule->getOrInsertFunction("poolmakeunfreeable", PoolMakeUnfreeableTy);
	      new CallInst(PoolMakeUnfreeable, make_vector(V, 0), "", 
			   CI->getNext());
	      moduleChanged = true;
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolA) {
	      FuncPoolAllocs[V].insert(cast<Instruction>(*UI));
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolF) {
	      FuncPoolFrees[V].insert(cast<Instruction>(*UI));
	    } else if (calledF->getName() == EmbeCFreeRemoval::PoolD) {
	      FuncPoolDestroys[V].insert(cast<Instruction>(*UI));
	    } else {
	      hasError = true;
	      std::cerr << "EmbeC: " << F->getName() << ": Unrecognized pool variable use \n";
	    }
	  } 
	} else {
	  DSGraph &TDG = TDDS->getDSGraph(*F);
	  DSNode *DSN = TDG.getNodeForValue(CI->getOperand(0)).getNode();
	  std::vector<GlobalValue*> Callees = DSN->getGlobals();
	  if (Callees.size() > 0) { 
	    // indirect function call

	    // Find the formal parameter corresponding to the parameter V
	    int operandNo;
	    for (unsigned int i = 1; i < CI->getNumOperands(); i++)
	      if (CI->getOperand(i) == V)
		operandNo = i;

	    for (std::vector<GlobalValue*>::iterator CalleesI = 
		   ++Callees.begin(), CalleesE = Callees.end(); 
		 CalleesI != CalleesE; ++CalleesI) {
	      Function *calledF = dyn_cast<Function>(*CalleesI);

	      if (PoolInfo->getFuncInfo(*calledF)->PoolArgFirst == 
		  PoolInfo->getFuncInfo(*calledF)->PoolArgLast ||
		  operandNo-1 < PoolInfo->getFuncInfo(*calledF)->PoolArgFirst ||
		  operandNo-1 >= PoolInfo->getFuncInfo(*calledF)->PoolArgLast)
		continue;

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
	}
      } else {
	hasError = true;
	std::cerr << "EmbeC: " << F->getName() << ": Unrecognized pool variable use \n";
      }   
    }
  }
}

// Returns true if BB1 follows BB2 in some path in F
static bool followsBlock(BasicBlock *BB1, BasicBlock *BB2, Function *F,
			 set<BasicBlock *> visitedBlocks) {
  if (succ_begin(BB2) != succ_end(BB2)) {
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


bool EmbeCFreeRemoval::run(Module &M) {
  CurModule = &M;
  moduleChanged = false;
  hasError = false;

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

  TDDS = &getAnalysis<TDDataStructures>();

  for (po_iterator<CallGraph*> CGI = po_begin(&CG), 
	 CGE = po_end(&CG); CGI != CGE; ++CGI) {
    
    Function *F = (*CGI)->getFunction();
    
    // Ignore nodes representing external functions in the call graph
    if (!F)
      continue;
    
    // All its pool SSA variables including its arguments
    set<Value *> FuncPoolPtrs;
    
    // Pool SSA variables that are used in allocs, destroy and free or calls 
    // to functions that escaped allocs, destroys and frees respectively.
    map<Value *, set<Instruction *> > FuncPoolAllocs, FuncPoolFrees, 
      FuncPoolDestroys;
    
    // Traverse the function finding poolfrees and calls to functions that
    // have poolfrees without pooldestroys on all paths in that function.
    
    if (!F->isExternal()) {
      // For each pool pointer def check its uses and ensure that there are 
      // no uses other than the pool_alloc, pool_free or pool_destroy calls
      
      
      PA::FuncInfo* PAFI = 0;
      
      // Calculating the FuncInfo for F
      for (Module::iterator ModI = CurModule->begin(), ModE = CurModule->end(); 
	   ModI != ModE;
	   ++ModI) {
	PA::FuncInfo* PFI = PoolInfo->getFuncInfo(*ModI);
	if (PFI) {
	  if (&*ModI == F || (PFI->Clone && PFI->Clone == F)) {
	    PAFI = PFI;
	    break;
	  } 
	}
      }

      
      // If the function has no pool pointers (args or SSA), ignore the 
      // function.
      if (!PAFI)
	continue;

      if (!PAFI->PoolDescriptors.empty()) {
	for (map<DSNode*, Value*>::iterator PAFII = 
	       PAFI->PoolDescriptors.begin(), 
	       PAFIE = PAFI->PoolDescriptors.end(); PAFII != PAFIE; ++PAFII) {
	  checkPoolSSAVarUses(F, (*PAFII).second, FuncPoolAllocs, 
			      FuncPoolFrees, FuncPoolDestroys);
	  FuncPoolPtrs.insert((*PAFII).second);
	}
      }
      else
	continue;

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
	  if (!(*ValueI).second.empty()) {
	    for (set<Instruction *>::iterator FreeInstI = 
		   (*ValueI).second.begin(), 
		   FreeInstE = (*ValueI).second.end();
		 FreeInstI != FreeInstE; ++FreeInstI) {
	      // For each free instruction or call to a function which escapes
	      // a free in pool (*ValueI).first
	      bool case3found = false, case2found = false;
	      for (set<Value *>::iterator PoolPtrsI = FuncPoolPtrs.begin(), 
		     PoolPtrsE = FuncPoolPtrs.end(); PoolPtrsI != PoolPtrsE; 
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
			 AllocInstI != AllocInstE; ++AllocInstI)
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
			 AllocInstI != AllocInstE; ++AllocInstI)
		      if (followsInst(*AllocInstI, *FreeInstI, F) &&
			  *AllocInstI != *FreeInstI)
			case2found = true;
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
	}
      }

      // Assumption: if we have pool_destroy on a pool in a function, then it
      // is on all exit paths of the function
      // TODO: correct later. 
      // Therefore, all pool ptr arguments that have frees but no destroys
      // escape the function. Similarly all pool ptr arguments that have
      // allocs but no destroys escape the function
      
      for (set<Value *>::iterator PoolPtrsI = FuncPoolPtrs.begin(), 
	     PoolPtrsE = FuncPoolPtrs.end(); PoolPtrsI != PoolPtrsE; 
	   ++PoolPtrsI) {
	if (isa<Argument>(*PoolPtrsI)) {
	  // Only for pool pointers that are arguments
	  if (FuncPoolFrees.find(*PoolPtrsI) != FuncPoolFrees.end())
	    FuncFreedPools[F].insert(*PoolPtrsI);
	  if (FuncPoolAllocs.find(*PoolPtrsI) != FuncPoolAllocs.end())
	    FuncAllocedPools[F].insert(*PoolPtrsI);
	  if (FuncPoolDestroys.find(*PoolPtrsI) != FuncPoolDestroys.end()) {
	    FuncDestroyedPools[F].insert(*PoolPtrsI);
	  }
	} 
      }

      // TODO
      // For each function, check that the frees in the function are safe i.e.
      // there are no mallocs between the free and its corresponding 
      // pool_destroy and then remove the pool free call.
    }
  }

  return moduleChanged;

}

Pass *createEmbeCFreeRemovalPass() { return new EmbeCFreeRemoval(); }

  

