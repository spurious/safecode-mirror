//===-- convert.cpp - EmbeC transformation that converts ------------//
// unsafe allocas to mallocs
// and updates the data structure analaysis accordingly
// Needs abcpre abc and checkstack safety 

#include "ConvertUnsafeAllocas.h"
#include "llvm/Instruction.h"
using namespace llvm;
using namespace CUA;
RegisterOpt<ConvertUnsafeAllocas> cua("convalloca", "converts unsafe allocas");

bool ConvertUnsafeAllocas::run(Module &M) {
    cssPass = &getAnalysis<checkStackSafety>();
    abcPass = &getAnalysis<ArrayBoundsCheck>();
    budsPass = &getAnalysis<BUDataStructures>();
    tddsPass = &getAnalysis<TDDataStructures>();
    unsafeAllocaNodes.clear();
    getUnsafeAllocsFromABC();
    TransformAllocasToMallocs(unsafeAllocaNodes);
    TransformAllocasToMallocs(cssPass->AllocaNodes);
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

void ConvertUnsafeAllocas::TransformAllocasToMallocs(std::vector<DSNode *> & unsafeAllocas) {
    std::vector<DSNode *>::const_iterator iCurrent = unsafeAllocas.begin(), iEnd = unsafeAllocas.end();
    for (; iCurrent != iEnd; ++iCurrent) {
	//I am just changing the info in the DSNode ...
	DSNode *DSN = *iCurrent;
	//	assert((DSN->isAllocaNode()) && "not an alloca, something wrong ");
	//DSN->maskNodeTypes(~DSNode::AllocaNode);
	DSN->setHeapNodeMarker();
	//Now change the alloca node to the malloc node
	//get the alloca instruction corresponding to the alloca node
	//create a new malloc instruction  
    }

}

DSNode * ConvertUnsafeAllocas::getDSNode(const Value *V, Function *F) {
	    DSGraph &TDG = budsPass->getDSGraph(*F);
	    DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
	    return DSN;
  
}


DSNode * ConvertUnsafeAllocas::getTDDSNode(const Value *V, Function *F) {
	    DSGraph &TDG = tddsPass->getDSGraph(*F);
	    DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
	    return DSN;
  
}

void ConvertUnsafeAllocas::getUnsafeAllocsFromABC() {
    std::vector<Instruction *> & UnsafeGetElemPtrs = abcPass->UnsafeGetElemPtrs;
    std::vector<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs.begin(), iEnd = UnsafeGetElemPtrs.end();
    for (; iCurrent != iEnd; ++iCurrent) {
	if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(*iCurrent)) {
	    Value *pointerOperand = GEP->getPointerOperand();
	    DSGraph &TDG = budsPass->getDSGraph(*(GEP->getParent()->getParent()));
	    DSNode *DSN = TDG.getNodeForValue(pointerOperand).getNode();
	    markReachableAllocas(DSN);
	}
    }
}
