//===-- SCPoolHeuristic.h - SAFECode Pool Allocation Heuristic ------------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines a heuristic class that pool allocates a program
// according to SAFECode's requirements.
//
//===----------------------------------------------------------------------===//

#include "poolalloc/Heuristic.h"

#include "llvm/Module.h"

namespace llvm {
namespace PA {
  //
  // Class: SCHeuristic 
  //
  // Description:
  //  This class provides a pool allocation heuristic that forces all DSNodes
  //  to be pool allocated.  Unlike the AllNodes heuristic from pool
  //  allocation, this heuristic will also pool allocate globals and stack
  //  objects.
  //
  class SCHeuristic: public Heuristic, public ModulePass {
    protected:
      /// Find globally reachable DSNodes that need a pool
      virtual void findGlobalPoolNodes (DSNodeSet_t & Nodes);

    public:
      // Pass ID
      static char ID;

      // Method used to implement analysis groups without C++ inheritance
      virtual void *getAdjustedAnalysisPointer(const PassInfo *PI) {
        if (PI->isPassID(&Heuristic::ID))
          return (Heuristic*)this;
        return this;
      }

      SCHeuristic (intptr_t IDp = (intptr_t) (&ID)): ModulePass (IDp) { }
      virtual ~SCHeuristic () {return;}
      virtual bool runOnModule (Module & M);
      virtual const char * getPassName () const {
        return "SAFECode Pool Allocation Heurisitic";
      }

      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        // We require DSA while this pass is still responding to queries
        AU.addRequiredTransitive<EQTDDataStructures>();

        // Make PassManager happy be requiring the default implementation of
        // this analysis group
        AU.addRequiredTransitive<Heuristic>();

        // This pass does not modify anything when it runs
        AU.setPreservesAll();
      }

      /// Find DSNodes local to a function that need a pool
      virtual void getLocalPoolNodes (const Function & F, DSNodeList_t & Nodes);

      //
      // Interface methods
      //
      virtual void AssignToPools (const DSNodeList_t & NodesToPA,
                                  Function *F, DSGraph* G,
                                  std::vector<OnePool> &ResultPools);
  };
}
}


