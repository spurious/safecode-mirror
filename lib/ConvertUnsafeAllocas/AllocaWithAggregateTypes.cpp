#include "llvm/Pass.h"
#include "llvm/BasicBlock.h"
#include "llvm/iMemory.h"
#include "llvm/iOther.h"
#include "llvm/Type.h"
#include "ConvertUnsafeAllocas.h"
#include "llvm/Function.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "Support/VectorExtras.h"

#include <iostream>

using namespace llvm;

namespace
{

  // Create the command line option for the pass
  //  RegisterOpt<MallocPass> X ("malloc", "Alloca to Malloc Pass");

  inline bool
  MallocPass::changeType (Instruction * Inst)
  {
    //
    // Check to see if the instruction is an alloca.
    //
    if (Inst->getOpcode() == Instruction::Alloca)
      {
	AllocationInst * AllocInst = cast<AllocationInst>(Inst);
	
	//
	// Get the type of object allocated.
	//
	const Type * TypeCreated = AllocInst->getAllocatedType ();
	
	//
	// If the allocated type is a structure, see if it has a pointer.
	//
	if (TypeCreated->getPrimitiveID() == Type::StructTyID)
	  {
	    const StructType * TheStruct = cast<StructType>(TypeCreated);
	    unsigned numE = TheStruct->getNumElements();

	    //
	    // Scan through the elements of the structure.  If we find a pointer.
	    // then we have to change it.
	    //
	    for (unsigned index = 0; index < numE; index++) {
	      if (TheStruct->getElementType(index)->getPrimitiveID() 
		  == Type::PointerTyID) {
		return true;
	      }
	    }
	    return false;
	  }
	
	//
	// If the type is an array, see if it is an array of pointers.
	//
	if (TypeCreated->getPrimitiveID() == Type::ArrayTyID)
	  {
	    const ArrayType * TheArrayType = cast<ArrayType>(TypeCreated);

	    //
	    // Get the type of element that lives in the array.
	    //
	    const Type * ElementType = TheArrayType->getElementType();
	    if (ElementType->getPrimitiveID() == Type::PointerTyID)
	      {
		return true;
	      }
	  }
      }

    return false;
  }

  bool
  MallocPass::runOnFunction (Function &F)
  {
    bool modified = false;
    TargetData &TD = getAnalysis<TargetData>();
    Module *theM = F.getParent();  
    Function *memsetF = 
      theM->getOrInsertFunction("memset", Type::VoidTy, 
				PointerType::get(Type::SByteTy), Type::IntTy , 
				Type::UIntTy, 0);
    for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
      {
	for (BasicBlock::iterator IAddrBegin=I->begin(), IAddrEnd = I->end();
	     IAddrBegin != IAddrEnd;
	     ++IAddrBegin)
	  {
	    //
	    // Determine if the instruction needs to be changed.
	    //
	    if (changeType (IAddrBegin))
	      {
		AllocationInst * AllocInst = cast<AllocationInst>(IAddrBegin);
		/*
		//
		// Get the type of object allocated.
		//
		const Type * TypeCreated = AllocInst->getAllocatedType ();
		
		MallocInst * TheMalloc = 
		new MallocInst(TypeCreated, 0, AllocInst->getName(), 
		IAddrBegin);
		std::cerr << "Found alloca that is struct or array" << std::endl;

		//
		// Remove old uses of the old instructions.
		//
		AllocInst->replaceAllUsesWith (TheMalloc);

		//
		// Remove the old instruction.
		//
		AllocInst->getParent()->getInstList().erase(AllocInst);
		modified = true;
		*/

		CastInst *CastI = 
		  new CastInst(AllocInst, 
			       PointerType::get(Type::SByteTy), "casted", AllocInst->getNext());
		std::vector<Value *> args(1, CastI);
		args.push_back(ConstantSInt::get(Type::IntTy,204));
		args.push_back(ConstantUInt::get(Type::UIntTy,
						 TD.getTypeSize(AllocInst->getType())));
		CallInst *CI = 
		  new CallInst(memsetF, args, "", CastI->getNext());
	      }
	  }
      }
    return modified;
  }

  RegisterOpt<MallocPass> Z("convallocai", "converts unsafe allocas to mallocs");
} // end of namespace

