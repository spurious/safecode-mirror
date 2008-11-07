#ifndef SOURCE_LOCATION_H
#define SOURCE_LOCATION_H

#include "llvm/Pass.h"
#include "llvm/Value.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IntrinsicInst.h"
#include <string>
#include <ostream>

namespace llvm {
struct SourceLocation {
	Value * name;
	Value * directory;
	Value * filename;
	unsigned lineNo;
	unsigned colNo;
	bool preciselyDefined() const { return filename && lineNo; }
#ifndef NDEBUG	
	void dump();
#endif
	SourceLocation() : name(0), directory(0), filename(0), lineNo(0), colNo(0) {}
	void print(std::ostream&) const;
};

inline std::ostream &operator<<(std::ostream &OS, const SourceLocation& SL) {
  SL.print(OS);
  return OS;
}

struct ValueLocation {
	bool isStatement;
	SourceLocation variable;
	SourceLocation type;
	SourceLocation statement;
	llvm::TypeDesc *typeDesc;
#ifndef NDEBUG	
	void dump();
#endif
	void print(std::ostream&) const;
	ValueLocation() : typeDesc(NULL) {}
};

inline std::ostream &operator<<(std::ostream &OS, const ValueLocation& VL) {
  VL.print(OS);
  return OS;
}

class ValueLocator {
	private:
		const llvm::Module *M;
		llvm::DIDeserializer DR;
		ValueLocation* getValueInfo(const llvm::DbgDeclareInst* DI);
		void printGEPIndices(ValueLocation *vLoc, const llvm::GetElementPtrInst *GEPI);
	public:
		void setModule(const llvm::Module *M) { this->M = M;}
		void printValue(std::ostream& out, const llvm::Value* V);
		ValueLocation* getValueLocation(const llvm::Value* V);
		ValueLocation* getInstrLocation(const llvm::Instruction* I);
};


class SourceLocator : public llvm::FunctionPass {
	private:
		SourceLocation* location;
	public:
		SourceLocator() : FunctionPass((intptr_t)&ID), location(NULL) {}
		static char ID;
		virtual bool runOnFunction(llvm::Function &F);

		// returns a copy of the location info for the current function
		SourceLocation* getLocation() const { return location; }

		void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
			AU.setPreservesAll();
		}
};
}
#endif
