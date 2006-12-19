//===-- convert.cpp - EmbeC transformation that converts ------------//
// unsafe allocas to mallocs
// and updates the data structure analaysis accordingly
// Needs abcpre abc and checkstack safety 

#include "safecode/Config/config.h"
#include "ConvertUnsafeAllocas.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Instruction.h"
#include "llvm/Support/CFG.h"
#include "llvm/Analysis/Dominators.h"

#include <iostream>

using namespace llvm;
using namespace CUA;
using namespace ABC;

extern DominatorSet::DomSetMapType dsmt;
extern DominanceFrontier::DomSetMapType dfmt;

static bool dominates(BasicBlock *bb1, BasicBlock *bb2) {
  DominatorSet::DomSetMapType::const_iterator dsmtI = dsmt.find(bb1);
  assert((dsmtI != dsmt.end()) && " basic block not found in dominator set");
  return (dsmtI->second.count(bb2) != 0);
}

//
// Statistics
//
static Statistic ConvAllocas ("convalloca", "Number of converted allocas");

RegisterPass<ConvertUnsafeAllocas> cua("convalloca", "converts unsafe allocas");

bool ConvertUnsafeAllocas::runOnModule(Module &M) {
  //
  // Retrieve all pre-requisite analysis results from other passes.
  //
  budsPass = &getAnalysis<CompleteBUDataStructures>();
  cssPass = &getAnalysis<checkStackSafety>();
  abcPass = &getAnalysis<ArrayBoundsCheck>();
#if 0
  tddsPass = &getAnalysis<TDDataStructures>();
#endif
  TD = &getAnalysis<TargetData>();
#ifdef LLVA_KERNEL
  //
  // Get a reference to the kmalloc() function (the Linux kernel's general
  // memory allocator function).
  //
  std::vector<const Type *> Arg(1, Type::UIntTy);
  Arg.push_back(Type::IntTy);
  FunctionType *kmallocTy = FunctionType::get(PointerType::get(Type::SByteTy), Arg, false);
  kmalloc = M.getOrInsertFunction("kmalloc", kmallocTy);

  //
  // If we fail to get the kmalloc function, generate an error.
  //
  assert ((kmalloc != 0) && "No kmalloc function found!\n");
#endif

  unsafeAllocaNodes.clear();
  getUnsafeAllocsFromABC();
  //  TransformCSSAllocasToMallocs(cssPass->AllocaNodes);
  //  TransformAllocasToMallocs(unsafeAllocaNodes);
  //  TransformCollapsedAllocas(M);
  return true;
}

bool ConvertUnsafeAllocas::markReachableAllocas(DSNode *DSN) {
  reachableAllocaNodes.clear();
  return   markReachableAllocasInt(DSN);
}

bool ConvertUnsafeAllocas::markReachableAllocasInt(DSNode *DSN) {
  bool returnValue = false;
  reachableAllocaNodes.insert(DSN);
  if (DSN->isAllocaNode()) {
    returnValue =  true;
    unsafeAllocaNodes.push_back(DSN);
  }
  for (unsigned i = 0, e = DSN->getSize(); i < e; i += DS::PointerSize)
    if (DSNode *DSNchild = DSN->getLink(i).getNode()) {
      if (reachableAllocaNodes.find(DSNchild) != reachableAllocaNodes.end()) {
        continue;
      } else if (markReachableAllocasInt(DSNchild)) {
        returnValue = returnValue || true;
      }
    }
  return returnValue;
}

void ConvertUnsafeAllocas::InsertFreesAtEnd(MallocInst *MI) {
  //need to insert a corresponding free
  // The dominator magic again
  BasicBlock *currentBlock = MI->getParent();
  DominanceFrontier::const_iterator it = dfmt.find(currentBlock);
  if (it != dfmt.end()) {
    const DominanceFrontier::DomSetType &S = it->second;
      if (S.size() > 0) {
	DominanceFrontier::DomSetType::iterator pCurrent = S.begin(), pEnd = S.end();
	for (; pCurrent != pEnd; ++pCurrent) {
	  BasicBlock *frontierBlock = *pCurrent;
	  //One of its predecessors is dominated by
	  // currentBlock
	  //need to insert a free in that predecessor
	  for (pred_iterator SI = pred_begin(frontierBlock), SE = pred_end(frontierBlock);
	       SI != SE; ++SI) {
	    BasicBlock *predecessorBlock = *SI;
	    if (dominates(predecessorBlock, currentBlock)) {
	      //get the terminator
	      Instruction *InsertPt = predecessorBlock->getTerminator();
	      new FreeInst(MI, InsertPt);
	    } 
	  }
	}
      }
  } else {
    //There is no dominance frontier, need to insert on all returns;
    Function *F = MI->getParent()->getParent();
    std::vector<Instruction*> FreePoints;
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
      if (isa<ReturnInst>(BB->getTerminator()) ||
	  isa<UnwindInst>(BB->getTerminator()))
	FreePoints.push_back(BB->getTerminator());
    //we have the Free points
    //now we get
    //	    Construct the free instructions at each of the points.
    std::vector<Instruction*>::iterator fpI = FreePoints.begin(), fpE = FreePoints.end();
    for (; fpI != fpE ; ++ fpI) {
      Instruction *InsertPt = *fpI;
      new FreeInst(MI, InsertPt);
    }
  }
}

// Precondition: Enforce that the alloca nodes haven't been already converted
void ConvertUnsafeAllocas::TransformAllocasToMallocs(std::list<DSNode *> 
						     & unsafeAllocaNodes) {

  std::list<DSNode *>::const_iterator iCurrent = unsafeAllocaNodes.begin(), 
                                      iEnd     = unsafeAllocaNodes.end();

  for (; iCurrent != iEnd; ++iCurrent) {
    DSNode *DSN = *iCurrent;
    
    // Now change the alloca instruction corresponding to the node	
    // to malloc 
    DSGraph *DSG = DSN->getParentGraph();
    DSGraph::ScalarMapTy &SM = DSG->getScalarMap();

#ifndef LLVA_KERNEL    
    MallocInst *MI = 0;
#else
    CastInst *MI = 0;
#endif
    for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
         SMI != SME; ) {
      bool stackAllocate = true;
      // If this is already a heap node, then you cannot allocate this on the
      // stack
      if (DSN->isHeapNode()) {
        stackAllocate = false;
      }

      if (SMI->second.getNode() == DSN) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
          //create a new malloc instruction
          if (AI->getParent() != 0) {
#ifndef LLVA_KERNEL	  
            MI = new MallocInst(AI->getType()->getElementType(),
                                AI->getArraySize(), AI->getName(), AI);
#else
            Value *AllocSize =
            ConstantInt::get(Type::UIntTy,
                              TD->getTypeSize(AI->getAllocatedType()));
	    
            if (AI->isArrayAllocation())
              AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
                                                 AI->getOperand(0), "sizetmp",
                                                 AI);	    
            std::vector<Value *> args(1, AllocSize);
            const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
            ConstantInt * signedzero = ConstantInt::get(csiType,32);
            args.push_back(signedzero);
            CallInst *CI = new CallInst(kmalloc, args, "", AI);
            MI = new CastInst(CI, AI->getType(), "",AI);
#endif	    
	    DSN->setHeapNodeMarker();
	    AI->replaceAllUsesWith(MI);
	    SM.erase(SMI++);
	    AI->getParent()->getInstList().erase(AI);
            ++ConvAllocas;
	    //	    InsertFreesAtEnd(MI);
#ifndef LLVA_KERNEL	    
            if (stackAllocate) {
              ArrayMallocs.insert(MI);
            }
#endif	      
          } else {
            ++SMI;
          } 
        } else {
          ++SMI;
        }
      } else {
        ++SMI;
      }
    }
  }  
}

void ConvertUnsafeAllocas::TransformCSSAllocasToMallocs(std::vector<DSNode *> & cssAllocaNodes) {
  std::vector<DSNode *>::const_iterator iCurrent = cssAllocaNodes.begin(),
                                        iEnd     = cssAllocaNodes.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    DSNode *DSN = *iCurrent;

    if (DSN->isNodeCompletelyFolded())
      continue;

    // If this is already listed in the unsafeAllocaNode vector, remove it
    // since we are processing it here
    std::list<DSNode *>::iterator NodeI = find(unsafeAllocaNodes.begin(),
                                               unsafeAllocaNodes.end(),
                                               DSN);
    if (NodeI != unsafeAllocaNodes.end()) {
      unsafeAllocaNodes.erase(NodeI);
    }
    
    // Now change the alloca instructions corresponding to this node to mallocs
    DSGraph *DSG = DSN->getParentGraph();
    DSGraph::ScalarMapTy &SM = DSG->getScalarMap();
#ifndef LLVA_KERNEL    
    MallocInst *MI = 0;
#else
    CastInst *MI = 0;
#endif
    for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
         SMI != SME; ) {
      if (SMI->second.getNode() == DSN) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
          // Create a new malloc instruction
          if (AI->getParent() != 0) { //This check for both stack and array
#ifndef LLVA_KERNEL 	    
            MI = new MallocInst(AI->getType()->getElementType(),
                                AI->getArraySize(), AI->getName(), AI);
	    InsertFreesAtEnd(MI);
#else
            Value *AllocSize =
            ConstantInt::get(Type::UIntTy,
                              TD->getTypeSize(AI->getAllocatedType()));
	    
            if (AI->isArrayAllocation())
              AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
                                                 AI->getOperand(0), "sizetmp",
                                                 AI);	    
            std::vector<Value *> args(1, AllocSize);
            const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
            ConstantInt * signedzero = ConstantInt::get(csiType,32);
            args.push_back(signedzero);
            CallInst *CI = new CallInst(kmalloc, args, "", AI);
            MI = new CastInst(CI, AI->getType(), "",AI);
#endif	    
	    DSN->setHeapNodeMarker();
	    AI->replaceAllUsesWith(MI);
	    SM.erase(SMI++);
	    AI->getParent()->getInstList().erase(AI);
            ++ConvAllocas;
	  } else {
	    ++SMI;
	  }
	}else {
	  ++SMI;
	}
      }else {
	++SMI;
      }
    }
  }
}

DSNode * ConvertUnsafeAllocas::getDSNode(const Value *V, Function *F) {
  DSGraph &TDG = budsPass->getDSGraph(*F);
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
}


DSNode * ConvertUnsafeAllocas::getTDDSNode(const Value *V, Function *F) {
#if 0
  DSGraph &TDG = tddsPass->getDSGraph(*F);
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
#else
  return 0;
#endif
}

void ConvertUnsafeAllocas::TransformCollapsedAllocas(Module &M) {
  //Need to check if the following is incomplete becasue we are only looking at scalars.
  //It may be complete because every instruction actually is a scalar in LLVM?!
  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
    if (!MI->isExternal()) {
      DSGraph &G = budsPass->getDSGraph(*MI);
      DSGraph::ScalarMapTy &SM = G.getScalarMap();
      for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
           SMI != SME; ) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
          if (SMI->second.getNode()->isNodeCompletelyFolded()) {
#ifndef LLVA_KERNEL
            MallocInst *MI = new MallocInst(AI->getType()->getElementType(),
                                            AI->getArraySize(), AI->getName(), 
                                            AI);
#else
            Value *AllocSize =
            ConstantInt::get(Type::UIntTy,
                              TD->getTypeSize(AI->getAllocatedType()));
            if (AI->isArrayAllocation())
              AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
                                                 AI->getOperand(0), "sizetmp",
                                                 AI);	    

            std::vector<Value *> args(1, AllocSize);
            const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
            ConstantInt * signedzero = ConstantInt::get(csiType,32);
            args.push_back(signedzero);
            CallInst *CI = new CallInst(kmalloc, args, "", AI);
            CastInst * MI = new CastInst(CI, AI->getType(), "",AI);
#endif
            AI->replaceAllUsesWith(MI);
            SMI->second.getNode()->setHeapNodeMarker();
            SM.erase(SMI++);
            AI->getParent()->getInstList().erase(AI);	  
            ++ConvAllocas;
          } else {
            ++SMI;
          }
        } else {
          ++SMI;
        }
      }
    }
  }
}

void ConvertUnsafeAllocas::getUnsafeAllocsFromABC() {
  std::vector<Instruction *> & UnsafeGetElemPtrs = abcPass->UnsafeGetElemPtrs;
  std::vector<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs.begin(), iEnd = UnsafeGetElemPtrs.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(*iCurrent)) {
      Value *pointerOperand = GEP->getPointerOperand();
      DSGraph &TDG = budsPass->getDSGraph(*(GEP->getParent()->getParent()));
      DSNode *DSN = TDG.getNodeForValue(pointerOperand).getNode();
      //FIXME DO we really need this ?	    markReachableAllocas(DSN);
      if (DSN && DSN->isAllocaNode() && !DSN->isNodeCompletelyFolded()) {
        unsafeAllocaNodes.push_back(DSN);
      }
    } else {
      
      //call instruction add the corresponding 	  *iCurrent->dump();
      //FIXME 	  abort();
    }
  }
}
