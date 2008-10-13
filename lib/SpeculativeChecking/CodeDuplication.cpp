/// This file performs code duplication analysis and wraps codes into
/// functions

#include <set>
#include <iostream>
#include "safecode/CodeDuplication.h"
#include "llvm/Function.h"
#include "llvm/Transforms/Utils/Cloning.h"

static llvm::RegisterPass<llvm::CodeDuplicationAnalysis> sCodeDuplicationAnalysisPass("-code-dup-analysis", "Analysis for code duplication", false, false);

static llvm::RegisterPass<llvm::RemoveSelfLoopEdge> sRemoveSelfLoopEdgePass("-break-self-loop-edge", "Break all self-loop edges in basic blocks");

static llvm::RegisterPass<llvm::DuplicateCodeTransform> sDuplicateCodeTransformPass("-duplicate-code-transformation", "Duplicate codes for SAFECode checking");

namespace llvm {

  char CodeDuplicationAnalysis::ID = 0;

  /// Determine whether a basic block is eligible for code duplication
  /// Here are the criteria:
  ///
  /// 1. No call instructions (FIXME: what about internal function
  /// calls? )
  ///
  /// 2. Memory access patterns and control flows are memory
  /// indepdendent, i.e., the results of load instructions in the
  /// basic block cannot affect memory addresses and control flows.
  ///
  /// 3. Volative instructions(TODO: Implementation!)


  static bool isEligibleforCodeDuplication(BasicBlock * BB) {
    std::set<Instruction*> unsafeInstruction;
    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
      if (isa<CallInst>(I)) {
	return false;
      }

      /*   } else if (isa<LoadInst>(I)) {
	/// Taint analysis for Load Instructions
	LoadInst * inst = dyn_cast<LoadInst>(I);
	unsafeInstruction.insert(inst);
	for (Value::use_iterator I = inst->use_begin(), E = inst->use_end(); I != E; ++ I) {
	  if (Instruction * in = dyn_cast<Instruction>(I)) {
	    if (in->getParent() != BB) {
	      break;
	    }
	    unsafeInstruction.insert(in);
	  }
	}
      }
    }

    /// Check GEP instructions
    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
      if (isa<GetElementPtrInst>(I) && unsafeInstruction.count(I)) {
	return false;
      }
    }

    Instruction * termInst = BB->getTerminator();
    return unsafeInstruction.count(termInst) == 0;
      */
    }
      return true;
  }

  void CodeDuplicationAnalysis::calculateBBArgument(BasicBlock * BB, InputArgumentsTy & args) {
    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
      Instruction * inst = &*I;

      // Add Phi node and load instructions into input arguments
      if (isa<PHINode>(inst) || isa<LoadInst>(inst)) {
	args.push_back(inst);
	continue;
      }

      /// 
      for (User::op_iterator op_it = inst->op_begin(), op_end = inst->op_end(); op_it != op_end; ++op_it) {
	Value * def = op_it->get();
	// Def is outside of the basic block
	Instruction * defInst = dyn_cast<Instruction>(def);
	if (defInst && defInst->getParent() != BB) {
	  args.push_back(defInst);
	}
      }
    }
  }

  bool CodeDuplicationAnalysis::runOnModule(Module & M) {
    InputArgumentsTy args;

    for (Module::iterator FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
      for (Function::iterator BI = FI->begin(), BE = FI->end(); BI != BE; ++BI) {
	BasicBlock * BB = BI;
	args.clear();

	if (isEligibleforCodeDuplication(BB)) {
	  calculateBBArgument(BB, args);
	  mBlockInfo[BB] = args;
	  /*	  //	  BB->dump();
	  std::cerr << "=== args ===" << std::endl;
	  for (size_t i = 0; i < args.size(); ++i) {
	    //	    args[i]->getType()->dump();
	    //args[i]->dump();
	    std::cerr << std::endl;
	  }
	  std::cerr << "====" << std::endl;
	  */
	}
      }
    }
    return false;
  }
  
  bool CodeDuplicationAnalysis::doInitialization(Module & M) {
    mBlockInfo.clear();
    return false;
  }

  bool CodeDuplicationAnalysis::doFinalization(Module & M) {
    mBlockInfo.clear();
    return false;
  }

  ///
  /// RemoveSelfLoopEdge Methods
  ///

  char RemoveSelfLoopEdge::ID = 0;

  /// A a dummy basic block at the end of the input block to eliminate
  /// self-loop edges.
  static void removeBBSelfLoopEdge(BasicBlock * BB) {
    Instruction * inst = BB->getTerminator();
    BranchInst * branchInst = dyn_cast<BranchInst>(inst);
    BasicBlock * newEndBB = BasicBlock::Create(BB->getName() + ".self_loop_edge:");

    BranchInst::Create(BB, newEndBB);

    Function * F = BB->getParent();
    Function::iterator bbIt = BB;
    F->getBasicBlockList().insert(++bbIt, newEndBB);

    assert(branchInst && "the terminator of input basic block should be a branch instruction");

    for (User::op_iterator op_it = branchInst->op_begin(), op_end = branchInst->op_end(); op_it != op_end; ++op_it) {
      BasicBlock * bb = dyn_cast<BasicBlock>(op_it->get());
      if (BB == bb) {
	op_it->set(newEndBB);
      }
    }

    // Deal with PHI Node, from BreakCritcalEdges.cpp
    for (BasicBlock::iterator I = BB->begin(); isa<PHINode>(I); ++I) {
      PHINode *PN = cast<PHINode>(I);
      int BBIdx = PN->getBasicBlockIndex(BB);
      PN->setIncomingBlock(BBIdx, newEndBB);
    }
  }

  bool RemoveSelfLoopEdge::runOnFunction(Function & F) {
    typedef std::set<BasicBlock* > BasicBlockSetTy;
    BasicBlockSetTy toBeProceed;
    for (Function::iterator it = F.begin(), it_end = F.end(); it != it_end; ++it) {
      Instruction * inst = it->getTerminator();
      if (BranchInst * branchInst = dyn_cast<BranchInst>(inst)) {
	for (User::op_iterator op_it = branchInst->op_begin(), op_end = branchInst->op_end(); op_it != op_end; ++op_it) {
	  BasicBlock * bb = dyn_cast<BasicBlock>(op_it->get());
	  if (bb == &*it) {
	    toBeProceed.insert(bb);
	  }
	}
      }
    }

    for (BasicBlockSetTy::iterator it = toBeProceed.begin(), it_end = toBeProceed.end(); it != it_end; ++it) {
      removeBBSelfLoopEdge(*it);
    }

    return toBeProceed.size() != 0;
  }

  /// DuplicateCodeTransform Methods

  char DuplicateCodeTransform::ID = 0;

  bool DuplicateCodeTransform::runOnModule(Module &M) {
    CodeDuplicationAnalysis & CDA = getAnalysis<CodeDuplicationAnalysis>();
    for (CodeDuplicationAnalysis::BlockInfoTy::const_iterator it = CDA.getBlockInfo().begin(), 
	   it_end = CDA.getBlockInfo().end(); it != it_end; ++it) {
      wrapCheckingRegionAsFunction(M, it->first, it->second);
    }
    return true;
  }

  void DuplicateCodeTransform::wrapCheckingRegionAsFunction(Module & M, const BasicBlock * bb,
							    const CodeDuplicationAnalysis::InputArgumentsTy & args) {
    std::vector<const Type *> argType;
    for (CodeDuplicationAnalysis::InputArgumentsTy::const_iterator it = args.begin(), end = args.end(); it != end; ++it) {
      argType.push_back((*it)->getType());
    }

    FunctionType * FTy = FunctionType::get(Type::VoidTy,  argType, false);
    Function * F = Function::Create(FTy, GlobalValue::InternalLinkage, bb->getName() + ".dup", &M);

    /// Mapping from original def to function arguments
    typedef std::map<Value *, Argument *> DefToArgMapTy;
    DefToArgMapTy defToArgMap;
    Function::arg_iterator it_arg, it_arg_end;
    CodeDuplicationAnalysis::InputArgumentsTy::const_iterator it_arg_val, it_arg_val_end;
    for (it_arg = F->arg_begin(), it_arg_end = F->arg_end(), it_arg_val = args.begin(), it_arg_val_end = args.end();
	 it_arg != it_arg_end; ++it_arg, ++it_arg_val) {
      Value * argVal = *it_arg_val;
      it_arg->setName(argVal->getName() + ".dup");
      defToArgMap[argVal] = it_arg;
    }

    DenseMap<const Value*, Value*> valMapping;
    BasicBlock * newBB = CloneBasicBlock(bb, valMapping, "", F);
    Instruction * termInst = newBB->getTerminator();
    termInst->eraseFromParent();
    ReturnInst::Create(NULL, newBB);

    /// Replace defs inside the basic blocks with function arguments
    for (CodeDuplicationAnalysis::InputArgumentsTy::const_iterator it = args.begin(), end = args.end(); it != end; ++it) {
      if (valMapping.find(*it) != valMapping.end()) {
	Instruction * defInst = dyn_cast<Instruction>(valMapping[*it]);
	defInst->replaceAllUsesWith(defToArgMap[*it]);
        defInst->eraseFromParent();
      }
    }

    /// Eliminate stores
    std::set<Instruction *> toBeRemoved;
    for (BasicBlock::iterator it = newBB->begin(), end = newBB->end(); it != end; ++it) {
      if (isa<StoreInst>(it)) toBeRemoved.insert(it);
    }

    for (std::set<Instruction *>::iterator it = toBeRemoved.begin(), end = toBeRemoved.end(); it != end; ++it) {
      (*it)->removeFromParent();
    }

    /// Replace all uses whose defs
    for (BasicBlock::iterator it = newBB->begin(), end = newBB->end(); it != end; ++it) {
      for (DefToArgMapTy::iterator AI = defToArgMap.begin(), AE = defToArgMap.end(); AI != AE; ++AI) {
	it->replaceUsesOfWith(AI->first, AI->second);
      }

      for (DenseMap<const Value *, Value *>::iterator AI = valMapping.begin(), AE = valMapping.end(); AI != AE; ++AI) {
	it->replaceUsesOfWith(const_cast<Value*>(AI->first), AI->second);
      }
    }
  }

}
