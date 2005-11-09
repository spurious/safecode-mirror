//===-- convert.cpp - EmbeC transformation that converts ------------//
// unsafe allocas to mallocs
// and updates the data structure analaysis accordingly
// Needs abcpre abc and checkstack safety 

#include "ConvertUnsafeAllocas.h"
#include "llvm/Instruction.h"
using namespace llvm;
using namespace CUA;
RegisterOpt<ConvertUnsafeAllocas> cua("convalloca", "converts unsafe allocas");

bool ConvertUnsafeAllocas::runOnModule(Module &M) {
  budsPass = &getAnalysis<CompleteBUDataStructures>();
  cssPass = &getAnalysis<checkStackSafety>();
  abcPass = &getAnalysis<ArrayBoundsCheck>();
  //  tddsPass = &getAnalysis<TDDataStructures>();
  unsafeAllocaNodes.clear();
  getUnsafeAllocsFromABC();
  TransformAllocasToMallocs(cssPass->AllocaNodes, false);
  TransformAllocasToMallocs(unsafeAllocaNodes, true);
  TransformCollapsedAllocas(M);
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

void ConvertUnsafeAllocas::TransformAllocasToMallocs(std::vector<DSNode *> & unsafeAllocas, bool isArray) {
  std::vector<DSNode *>::const_iterator iCurrent = unsafeAllocas.begin(), iEnd = unsafeAllocas.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    //I am just changing the info in the DSNode ...
    DSNode *DSN = *iCurrent;
    bool storeMalloc = isArray;
    //	assert((DSN->isAllocaNode()) && "not an alloca, something wrong ");
    //DSN->maskNodeTypes(~DSNode::AllocaNode);
    //
    //Also, set the heap node marker in each of the callers
    
    
    //Now change the alloca node to the malloc node	
    //Get the alloca instruction corresponding to the alloca node
    DSGraph *DSG = DSN->getParentGraph();
    DSGraph::ScalarMapTy &SM = DSG->getScalarMap();
    //We only need to keep track of the alloca nodes that are unique
    //if multiple instructions point to this alloca node
    //then we dont need this alloca in ArrayMallocs list
    MallocInst *MI = 0;
    for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
	 SMI != SME; ++SMI) {
      if (SMI->second.getNode() == DSN) {
	Value *VI = SMI->first;
	if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
	      //create a new malloc instruction
	  if (AI->getParent() != 0) { //This check for both stack and array
	    if (MI) storeMalloc = false;
	    MI = new MallocInst(AI->getType()->getElementType(),AI->getArraySize(),
				AI->getName(), AI);
	    DSN->setHeapNodeMarker();
	    AI->replaceAllUsesWith(MI);
	    AI->getParent()->getInstList().erase(AI);
	  } else if (isa<MallocInst>(SMI->first)) {
	    storeMalloc = false;
	  }
	}
      }
    }
    if (storeMalloc && MI) {
      ArrayMallocs.insert(MI);
    }
  }
  
}

DSNode * ConvertUnsafeAllocas::getDSNode(const Value *V, Function *F) {
	    DSGraph &TDG = budsPass->getDSGraph(*F);
	    DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
	    return DSN;
  
}


DSNode * ConvertUnsafeAllocas::getTDDSNode(const Value *V, Function *F) {
  /*	    DSGraph &TDG = tddsPass->getDSGraph(*F);
	    DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
	    return DSN;
  */
  return 0;
  
}

void ConvertUnsafeAllocas::TransformCollapsedAllocas(Module &M) {
  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
    if (!MI->isExternal()) {
      DSGraph &G = budsPass->getDSGraph(*MI);
      DSGraph::ScalarMapTy &SM = G.getScalarMap();
      for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
	   SMI != SME; ++SMI) {
	if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
	  if (SMI->second.getNode()->isNodeCompletelyFolded()) {
	    MallocInst *MI = new MallocInst(AI->getType()->getElementType(),AI->getArraySize(),
					    AI->getName(), AI);
	    AI->replaceAllUsesWith(MI);
	    AI->getParent()->getInstList().erase(AI);	  
	  }
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
	  if (DSN && DSN->isAllocaNode()) {
	    unsafeAllocaNodes.push_back(DSN);
	  }
	} else {
	  //call instruction add the corresponding 	  *iCurrent->dump();
	  //FIXME 	  abort();
	}
    }
}
