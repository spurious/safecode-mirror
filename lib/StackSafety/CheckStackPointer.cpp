//===-- StackSafety.cpp - Analysis for ensuring stack safety ------------//
// Implementation of StackSafety.h
//            


#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/iTerminators.h"
#include "llvm/BasicBlock.h"
#include "llvm/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/InstIterator.h"
#include "StackSafety.h"
using namespace llvm;
std::ostream & Out = std::cout;  
 
using namespace CSS;

RegisterOpt<checkStackSafety> css("css1", "check stack safety");
  


bool checkStackSafety::markReachableAllocas(DSNode *DSN) {
  reachableAllocaNodes.clear();
  return   markReachableAllocasInt(DSN);
}

bool checkStackSafety::markReachableAllocasInt(DSNode *DSN) {
  bool returnValue = false;
  reachableAllocaNodes.insert(DSN);
  if (DSN->isAllocaNode()) {
    returnValue =  true;
    AllocaNodes.push_back(DSN);
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

bool checkStackSafety::run(Module &M) {
  
  TDDataStructures *TDDS;
  TDDS = &getAnalysis<TDDataStructures>();

  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
    Function &F = *MI;
    
    if (!F.isExternal()) {
      DSGraph &TDG = TDDS->getDSGraph(F);
      
      // check if return value is a  pointers
      if (isa<PointerType>(F.getReturnType())) {
	//return value type is a pointer
	for(inst_iterator ii = inst_begin(F), ie = inst_end(&F); ii != ie; ++ii) {
	  if (ReturnInst *RI = dyn_cast<ReturnInst>(*ii)) {
	    DSNode *DSN = TDG.getNodeForValue(RI).getNode();
	    if (DSN && markReachableAllocas(DSN)) {
	      std::cerr << "Instruction : \n" << RI << "points to a stack location\n";
	      std::cerr << "In Function " << F.getName() << "\n";
	      return false;
	    }
	  }
	}
      }
      Function::aiterator AI = F.abegin(), AE = F.aend();
      for (; AI != AE; ++AI) {
	if (isa<PointerType>(AI->getType())) {
	  DSNode *DSN = TDG.getNodeForValue(AI).getNode();
	  if (markReachableAllocas(DSN)) {
	    std::cerr << "Instruction : \n" << AI << "points to a stack location\n";
	    std::cerr << "In Function " << F.getName() << "\n";
	    
	  }
	}
      }
    }
  }
  return true;
}


Pass *createStackSafetyPass() { return new CSS::checkStackSafety(); }



