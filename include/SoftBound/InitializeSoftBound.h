// Prototype creator for SoftBoundPass

#ifndef INITIALIZE_SOFTBOUND_H
#define INITIALIZE_SOFTBOUND_H

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/LLVMContext.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Instruction.h"
#include "llvm/Target/TargetData.h"


using namespace llvm;

class InitializeSoftBound: public ModulePass {

 private:

 public:
  bool runOnModule(Module &);
  static char ID;

  void constructCheckHandlers(Module &);
  void constructMetadataHandlers(Module &);
  void constructShadowStackHandlers(Module &);
  void constructAuxillaryFunctionHandlers(Module &);
  InitializeSoftBound(): ModulePass(ID){        
  }
  
  const char* getPassName() const { return "InitializeSoftBound";}
};

#endif
