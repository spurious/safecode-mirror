//===-- convert.cpp - EmbeC transformation that converts ------------//
// unsafe allocas to mallocs
// and updates the data structure analaysis accordingly
// Needs abcpre abc and checkstack safety 

#include "ConvertUnsafeAllocas.h"
#include "llvm/Instruction.h"

#include <iostream>

using namespace llvm;
using namespace CUA;
#define LLVA_KERNEL 1
RegisterOpt<ConvertUnsafeAllocas> cua("convalloca", "converts unsafe allocas");

bool ConvertUnsafeAllocas::runOnModule(Module &M) {
  budsPass = &getAnalysis<CompleteBUDataStructures>();
  cssPass = &getAnalysis<checkStackSafety>();
  abcPass = &getAnalysis<ArrayBoundsCheck>();
  //  tddsPass = &getAnalysis<TDDataStructures>();
  TD = &getAnalysis<TargetData>();
  std::vector<const Type *> Arg(1, Type::UIntTy);
  Arg.push_back(Type::IntTy);
  FunctionType *kmallocTy = FunctionType::get(PointerType::get(Type::SByteTy), Arg, false);
  kmalloc = M.getFunction("kmalloc", kmallocTy);
  unsafeAllocaNodes.clear();
  getUnsafeAllocsFromABC();
  TransformCSSAllocasToMallocs(cssPass->AllocaNodes);
  TransformAllocasToMallocs(unsafeAllocaNodes);
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

// Precondition: Enforce that the alloca nodes haven't been already converted
void ConvertUnsafeAllocas::TransformAllocasToMallocs(std::list<DSNode *> 
						     & unsafeAllocaNodes) {
  std::list<DSNode *>::const_iterator iCurrent = unsafeAllocaNodes.begin(), 
    iEnd = unsafeAllocaNodes.end();
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
	      ConstantUInt::get(Type::UIntTy, TD->getTypeSize(AI->getAllocatedType()));
	    
    if (AI->isArrayAllocation())
      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 AI->getOperand(0), "sizetmp", AI);	    
	    std::vector<Value *> args(1, AllocSize);
	    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	    ConstantSInt * signedzero = ConstantSInt::get(csiType,32);
	    args.push_back(signedzero);
	    CallInst *CI = new CallInst(kmalloc, args, "", AI);
	    MI = new CastInst(CI, AI->getType(), "",AI);
#endif	    
	    DSN->setHeapNodeMarker();
	    AI->replaceAllUsesWith(MI);
	    SM.erase(SMI++);
	    AI->getParent()->getInstList().erase(AI);
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
  std::vector<DSNode *>::const_iterator iCurrent = cssAllocaNodes.begin(), iEnd = cssAllocaNodes.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    DSNode *DSN = *iCurrent;

    if (DSN->isNodeCompletelyFolded())
      continue;

    // If this is already listed in the unsafeAllocaNode vector, remove it
    // since we are processing it here
    std::list<DSNode *>::iterator NodeI = find(unsafeAllocaNodes.begin(),
					       unsafeAllocaNodes.end(),
					       DSN);
    if (NodeI != unsafeAllocaNodes.end())
    {
      unsafeAllocaNodes.erase(NodeI);
    }
    
    //Now change the alloca instructions corresponding to this node to mallocs
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
	  //create a new malloc instruction
	  if (AI->getParent() != 0) { //This check for both stack and array
#ifndef LLVA_KERNEL 	    
	    MI = new MallocInst(AI->getType()->getElementType(),AI->getArraySize(),
				AI->getName(), AI);
#else
	    Value *AllocSize =
	      ConstantUInt::get(Type::UIntTy, TD->getTypeSize(AI->getAllocatedType()));
	    
	    if (AI->isArrayAllocation())
	      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 AI->getOperand(0), "sizetmp", AI);	    
	    std::vector<Value *> args(1, AllocSize);
	    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	    ConstantSInt * signedzero = ConstantSInt::get(csiType,32);
	    args.push_back(signedzero);
	    CallInst *CI = new CallInst(kmalloc, args, "", AI);
	    MI = new CastInst(CI, AI->getType(), "",AI);
#endif	    
	    DSN->setHeapNodeMarker();
	    AI->replaceAllUsesWith(MI);
	    SM.erase(SMI++);
	    AI->getParent()->getInstList().erase(AI);
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
	   SMI != SME; ) {
	if (AllocaInst *AI = dyn_cast<AllocaInst>(SMI->first)) {
	  if (SMI->second.getNode()->isNodeCompletelyFolded()) {
#ifndef LLVA_KERNEL
	    MallocInst *MI = new MallocInst(AI->getType()->getElementType(),
					    AI->getArraySize(), AI->getName(), 
					    AI);
#else
	    Value *AllocSize =
	      ConstantUInt::get(Type::UIntTy, TD->getTypeSize(AI->getAllocatedType()));
      if (AI->isArrayAllocation())
        AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
             AI->getOperand(0), "sizetmp", AI);	    

      std::vector<Value *> args(1, AllocSize);
      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
      ConstantSInt * signedzero = ConstantSInt::get(csiType,32);
      args.push_back(signedzero);
      CallInst *CI = new CallInst(kmalloc, args, "", AI);
      CastInst * MI = new CastInst(CI, AI->getType(), "",AI);
#endif
	    AI->replaceAllUsesWith(MI);
	    SMI->second.getNode()->setHeapNodeMarker();
	    SM.erase(SMI++);
	    AI->getParent()->getInstList().erase(AI);	  
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
