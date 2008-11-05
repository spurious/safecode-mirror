//===- SourceLocator.cpp - Utility classes for reading LLVM debug data -------//
// 
// Code contributed by Edwin Torok.
//
//===----------------------------------------------------------------------===//
//
// This file implements classes that can be used to easily manipulate debug
// information stored within LLVM debug intrinsics.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sourcelocator"

#include "llvm/Support/Debug.h"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Instructions.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Module.h"

#include "safecode/SourceLocator.h"

#include <sstream>

using namespace llvm;

void SourceLocation::print(std::ostream& out) const
{
  // FIXME: Enable this again!
#if 0
	if (filename.empty()) {
		out << "Unknown location (" << name << ")";
	} else {
		//TODO: transform this into a relative path to current directory
		//iff by doing so the path becomes shorter!
		out << name << "(in ";
		if(!directory.empty())
			out << directory << "/";
		out << filename;
		if(lineNo) {
			out << ":" << lineNo;
			if(colNo) {
				out << ":" << colNo;
			}
		}
		out << ")";
	}
#endif
}

void ValueLocation::print(std::ostream& out) const
{
//         if(!isStatement) {
		 if(type.preciselyDefined()) {
			 type.print(out);
		 } else {
			 out << type.name;
		 }
		 out << " ";
		 variable.print(out);
//	 } else {
		 statement.print(out);
//	 }
}

#ifndef NDEBUG
void SourceLocation::dump()
{
	llvm::cerr << *this;
}

void ValueLocation::dump()
{
  cerr << "variable ";
  variable.dump();
  cerr <<" of type ";
  type.dump();
  cerr << "statement ";
  statement.dump();
}
#endif

ValueLocation* ValueLocator::getInstrLocation(const Instruction* I)
{
	ValueLocation* vloc = getValueLocation(I);
	const BasicBlock* BB = I->getParent();
	BasicBlock::const_iterator ci = BB->begin();
	BasicBlock::const_iterator ce = BB->end();
	//set iterator to current Value
	while(&*ci != I && ci != ce) ++ci;
	assert(&*ci == I && "Value must be found inside its own BasicBlock");

	//look for a stoppoint
	for(;ci != ce; ++ci) {
		if(const DbgStopPointInst *SPI = dyn_cast<DbgStopPointInst>(&*ci)) {
			struct SourceLocation* sloc = &vloc->statement;
			sloc->filename = SPI->getFileName();
			sloc->directory = SPI->getDirectory();
			sloc->lineNo = SPI->getLine();
			sloc->colNo = SPI->getColumn();
			vloc->isStatement = true;
			return vloc;
		}
	}

	const Function *F = I->getParent()->getParent();
  // FIXME: Need to turn this back on
#if 0
	vloc->statement.filename = F->getParent()->getModuleIdentifier() + ":" + F->getName();
#endif
	vloc->isStatement = true;
	return vloc;
}


//
// Function: findDirectDeclaration()
//
// Description:
//  Attempt to find an LLVM DbgDeclare intrinsic that provides direct debugging
//  information about the specified LLVM value.
//
// Return value:
//  NULL - No direct debug declaration intrinsic for the value was found.
//  Otherwise, a pointer to the debug declaration intrinsic is returned.
//
static const DbgDeclareInst*
findDirectDeclaration (const Value* V) {
  // Scan all uses of the value to see if a DbgDeclareInst uses it
	for (Value::use_const_iterator i = V->use_begin(), e = V->use_end();
       i != e; ++i) {
		const User* U = *i;
		if (const DbgDeclareInst* DI = dyn_cast<DbgDeclareInst>(U)) {
			return DI;
		}

    //
    // It's possible that the value is casted and then used by a DbgDeclareInst
    // intrinsic.  Peer past the cast.
    //
		if (isa<BitCastInst>(U)) {
			const DbgDeclareInst* DI = findDirectDeclaration(U);
			if (DI) {
				return DI;
			}
		}
	}

  // No direct debug declaration of the value has been found.
	return NULL;
}


ValueLocation*
ValueLocator::getValueInfo (const DbgDeclareInst* DI)
{
	ValueLocation* vinfo = new ValueLocation();
	vinfo->isStatement = false;
	Value* V = DI->getVariable();
	VariableDesc* vd = cast<VariableDesc>(DR.Deserialize(V));
	SourceLocation* variableLoc = &vinfo->variable;
	CompileUnitDesc* varUnit = vd->getFile();

  //
	// We prefer to use name from debug-info, since this reflects the original
  // name of the variable (same as in source file).
  //
  // FIXME: Need to get this working
  //
#if 0
	variableLoc->name = vd->getName();
	variableLoc->directory = varUnit->getDirectory();
	variableLoc->filename = varUnit->getFileName();
	variableLoc->lineNo = vd->getLine();
#endif

	TypeDesc* td = vd->getType();
	if (td) {
		SourceLocation* typeLoc = &vinfo->type;
		typeLoc->name = ConstantArray::get (td->getName());
		typeLoc->lineNo = td->getLine();
		CompileUnitDesc* typeUnit = vd->getFile();
		typeLoc->directory = ConstantArray::get (typeUnit->getDirectory());
		typeLoc->filename = ConstantArray::get (typeUnit->getFileName());
		vinfo->typeDesc = td;
	}
	return vinfo;
}

void ValueLocator::printValue(std::ostream& out, const Value* V)
{
	if(isa<Constant>(V)) {
		if(const ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
			out << CI->getSExtValue();
		} else {
			out << *V;
		}
	} else {
		ValueLocation* aLoc = getValueLocation(V);
		aLoc->print(out);
		delete aLoc;
	}
}

void ValueLocator::printGEPIndices(ValueLocation *vLoc, const GetElementPtrInst *GEPI)
{
	SmallVector<Value*, 8> IndicesVector(GEPI->idx_begin(), GEPI->idx_end());
	Value* const* Indices = &IndicesVector[0];
	const unsigned NumIndices = IndicesVector.size();
	const Type *Ty = GEPI->getOperand(0)->getType();

	TypeDesc *DebugTD = vLoc->typeDesc;

	generic_gep_type_iterator<Value* const*> TI = gep_type_begin(Ty, Indices, Indices+NumIndices);

	std::stringstream name;
	assert(M && "Module must be set");

	if(!isa<ConstantInt>(Indices[0]) || !cast<ConstantInt>(Indices[0])->isZero()) {
		//show only non-zero pointer arithmetic
		name << "&";
		name << vLoc->variable.name;
		name << "[";
		printValue(name, Indices[0]);
		name << "]";
	} else {
		name << vLoc->variable.name;
	}
	if(1 != NumIndices && DebugTD) {
		if(isa<CompositeTypeDesc>(DebugTD)) {
			DebugTD = cast<CompositeTypeDesc>(DebugTD)->getFromType();
		} else if(isa<DerivedTypeDesc>(DebugTD)) {
			DebugTD = cast<DerivedTypeDesc>(DebugTD)->getFromType();
		} else {
			DOUT << "Unknown!\n";
		}
	}
	++TI;
	for (unsigned CurIDX = 1; CurIDX != NumIndices; ++CurIDX, ++TI) {
		Value *IdxVal = Indices[CurIDX];
		Ty = *TI;
		if(isa<PointerType>(Ty) || isa<ArrayType>(Ty)) {
			name << "[";
			printValue(name, IdxVal);
			name << "]";
		} else if (isa<StructType>(Ty)) {
			int idx = cast<ConstantInt>(IdxVal)->getSExtValue();
			if(CompositeTypeDesc *CTD = dyn_cast_or_null<CompositeTypeDesc>(DebugTD)) {
				DebugInfoDesc * DID = CTD->getElements()[idx];
				DebugTD = cast<TypeDesc>(DID);
				name << "." << DebugTD->getName();
			} else {
				name << ".%" << idx << "%";
			}

		}
	}
	vLoc->typeDesc = DebugTD;
	vLoc->variable.name = ConstantArray::get (name.str());
}

//
// Method: getValueLocation()
//
// Description:
//  Allocate and initialize a new ValueLocation object for the given LLVM
//  value.
//
ValueLocation* ValueLocator::getValueLocation (const Value* V) {
  //  
  // If the value has no type, just create a no-name ValueLocation object.
  //
	if (!V->getType()) {
		ValueLocation* vinfo = new ValueLocation();
		vinfo->isStatement = false;
		vinfo->variable.name = ConstantArray::get (V->getNameStr());
		return vinfo;
	}

	//
  // Find the line in the original source corresponding to this value by
  // looking for the nearest stoppoint.
  //
	for (;;) {
    //
    // Attempt to find a debug intrinsic that declares information directly
    // about the value.  If we find it, use it.
    //
		const DbgDeclareInst* DI = findDirectDeclaration(V);
		if (DI) {
			return getValueInfo (DI);
		}

		if (const Instruction* I = dyn_cast<Instruction>(V)) {
      //
      // If it is a cast or load instruction, try to find the debug information
      // by checking its operand.
      //
			if (I->isCast() || isa<LoadInst>(I)) {
				// TODO:
        //  load dereferences, and is safe to pass through only if it is a
        //  local variable
				V = I->getOperand(0);
				continue;
			}

      //
      // If it is a GEP instruction, then try to find the debug information
      // by examining the source pointer.
      //
			if (const GetElementPtrInst* GEPI = dyn_cast<GetElementPtrInst>(I)) {
				ValueLocation* vLoc = getValueLocation( GEPI->getOperand(0) );
				printGEPIndices(vLoc, GEPI);
				return vLoc;
			}
		}

    //
		// Fill in information that we can get without using debug info.
		// Note that V->getNameStr() returns the LLVM name that can be a
    // temporary's name or the original name with a counter (in case of
    // multiple variables with same name scoped at same time).
    //
		ValueLocation* vinfo = new ValueLocation();
		vinfo->isStatement = false;
		vinfo->variable.name = ConstantArray::get (V->getNameStr());
		if (const Instruction *I = dyn_cast<Instruction>(V)) {
			const Function *F = I->getParent()->getParent();
			vinfo->variable.filename = ConstantArray::get (F->getParent()->getModuleIdentifier() + ":" + F->getName());
		}
		if(const SequentialType *ST = dyn_cast<SequentialType>(V->getType())) {
			vinfo->type.name = ConstantArray::get (ST->getElementType()->getDescription());
		} else {
			vinfo->type.name = ConstantArray::get (V->getType()->getDescription());
		}
		return vinfo;
	}
}
#if 0
	for(Value::use_const_iterator i = V->use_begin(), e = V->use_end(); i != e; ++i) {
		if(const BitCastInst *BI = dyn_cast<BitCastInst>(*i)) {
			for(Value::use_const_iterator j = BI->use_begin(), e = BI->use_end(); j != e; ++j) {
				if(const DbgDeclareInst *DI = dyn_cast<DbgDeclareInst>(*j)) {
					VariableDesc* vd = cast<VariableDesc>(DR.Deserialize(DI->getVariable()));
					SourceLocation* variableLoc = &vinfo->variable;
					CompileUnitDesc* varUnit = vd->getFile();
					/* we prefer to use name from debug-info, since
					 * this reflects the original name of the variable
					 * (same as in source file). */
					variableLoc->name = vd->getName();
					variableLoc->directory = varUnit->getDirectory();
					variableLoc->filename = varUnit->getFileName();
					variableLoc->lineNo = vd->getLine();

					TypeDesc* td = vd->getType();
					SourceLocation* typeLoc = &vinfo->type;
					typeLoc->name = td->getName();
					typeLoc->lineNo = td->getLine();
					CompileUnitDesc* typeUnit = vd->getFile();
					typeLoc->directory = typeUnit->getDirectory();
					typeLoc->filename = typeUnit->getFileName();
					return vinfo;
				}
			}
		}
	}
#endif


bool SourceLocator::runOnFunction(Function &F) {
	if(location) {
		delete location;
		location = NULL;
	}
	for(inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		if(const CallInst* CI = dyn_cast<CallInst>(&*i))
			if(CI->getCalledFunction())
				if(const DbgStopPointInst *SPI = dyn_cast<DbgStopPointInst>(CI)) {
					location = new SourceLocation();
					location->directory = SPI->getDirectory();
					location->filename = SPI->getFileName();
					location->lineNo = SPI->getLine();
					location->colNo = SPI->getColumn();
					break;
				}
	}
	return false;
}

char SourceLocator::ID = 0;
static RegisterPass<SourceLocator> X("source-locator","Source-locator Pass");
