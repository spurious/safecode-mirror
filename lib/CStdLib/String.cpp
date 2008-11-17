//===-------- String.cpp - Secure C standard string library calls ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass finds all calls to functions in the C standard string library and
// transforms them to a more secure form.
//
//===----------------------------------------------------------------------===//

#include "CStdLib.h"

ModulePass *llvm::createStringTransformPass() { return new StringTransform(); }

bool StringTransform::runOnModule(Module &M) {
	// Flags whether we modified the module
	bool modified = false;

	modified = strcpyTransform(M) ? true : false;

	return modified;
}

bool StringTransform::strcpyTransform(Module &M) {
	// Flags whether we modified the module
	bool modified = false;

	std::vector<const Type *> strncpyParameters;
	strncpyParameters.push_back(PointerType::get(Type::Int16Ty, 0));
	strncpyParameters.push_back(PointerType::get(Type::Int16Ty, 0));

	Function * strcpyFunc = M.getFunction("strcpy");
	if (!strcpyFunc)
		return modified;
	const Type * strncpyRT = strcpyFunc->getReturnType();
	FunctionType * strncpyFT = FunctionType::get(strncpyRT, strncpyParameters, false);
	Constant * strncpyFunction = M.getOrInsertFunction("strncpy", strncpyFT);

	//
	// Scan through the module and replace strcpy() with strncpy().
	//
	for (Module::iterator F=M.begin(); F != M.end(); ++F) {
		for (Function::iterator BB = F->begin(); BB != F->end(); ++BB) {
			for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
				//
				// If this is not a call instruction, just skip it.
				//
				if (!(isa<CallInst>(I)))
					continue;

				//
				// Otherwise, figure out if this is a system call.
				//
				CallInst * CI = dyn_cast<CallInst>(I);
				Function * CalledFunc = CI->getCalledFunction();

				if ((CalledFunc == 0) || (!(CalledFunc->hasName())))
					continue;

				if ((CalledFunc->getName() != "strcpy"))
					continue;

				//
				// If we can determine the mode, then attempt to convert the strcpy().
				//
				ConstantExpr * CE = (ConstantExpr *)(CI->getOperand(2));
				GlobalVariable * ModeGV = (GlobalVariable *)(CE->getOperand(0));
				if (ModeGV->hasInitializer()) {
					int bounds = 0; // getBounds()

					//
					// Create a strncpy() call.
					//
					std::vector<Value *> Params;
					Params.push_back( ConstantInt::get(Type::Int32Ty, bounds) );
					Params.push_back( CI->getOperand(1) );
					CallInst * strncpyCallInst = CallInst::Create(strncpyFunction, Params.begin(), Params.end(), "strings", CI);

					// Replace all of the uses of the strcpy() call with the new strncpy() call.
					//
					CI->replaceAllUsesWith(strncpyCallInst);
					CI->eraseFromParent();

					//
					// Mark the module as modified and continue to the next strcpy() call.
					//
					modified = true;
				}
			}
		}
	}

	return modified;
}

