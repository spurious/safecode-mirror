/**
 * $Id: InsertPoolChecks.h,v 1.25 2008-08-27 05:37:31 mai4 Exp $
 * vim: ts=2 sw=2 
 **/

#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "safecode/Config/config.h"
#include "llvm/Pass.h"
#include "ArrayBoundsCheck.h"
#include "ConvertUnsafeAllocas.h"

#ifndef LLVA_KERNEL
#include "SafeDynMemAlloc.h"
#include "poolalloc/PoolAllocate.h"
#endif

#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/LoopInfo.h" 

namespace llvm {

	ModulePass *creatInsertPoolChecks();
	using namespace CUA;
	struct MonotonicLoopOpt; 

	/**
	 *
	 * Insert function prototypes of checks
	 *
	 **/
	struct InsertCheckProtosPass : public ModulePass {
		friend struct InsertPoolChecks;
		public:
			static char ID;
		  InsertCheckProtosPass () : ModulePass ((intptr_t) &ID) {};
			virtual bool runOnModule (Module & M);
			virtual void getAnalysisUsage(AnalysisUsage &AU) const
			{
			  AU.addRequired<EquivClassGraphs>();
				AU.addRequired<ArrayBoundsCheck>();
				AU.addRequired<EmbeCFreeRemoval>();
				AU.addRequired<TargetData>();
			  AU.addPreserved<PoolAllocateGroup>();
			};
		private:
			// External functions in the SAFECode run-time library
			Constant *RuntimeInit;
			Constant *PoolCheck;
			Constant *PoolCheckUI;
			Constant *PoolCheckArray;
			Constant *PoolCheckArrayUI;
			Constant *ExactCheck;
			Constant *ExactCheck2;
			Constant *FunctionCheck;
			Constant *GetActualValue;
			Constant *StackFree;

			// Required Checks
			ArrayBoundsCheck * abcPass;
			PoolAllocateGroup * paPass;
			EmbeCFreeRemoval *efPass;
			TargetData * TD;
	};

	struct InsertPoolChecks : public ModulePass {
		friend struct MonotonicLoopOpt; 
		friend struct AddLoadStoreCheckPass; 
		private :
		// Flags whether we want to do dangling checks
		bool DanglingChecks;

		public :
		static char ID;
		InsertPoolChecks (bool DPChecks = false)
			: ModulePass ((intptr_t) &ID) {
				DanglingChecks = DPChecks;
			}
		const char *getPassName() const { return "Inserting pool checks for array bounds "; }
		virtual bool runOnModule(Module &M);
		virtual void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<InsertCheckProtosPass>();
			AU.addPreserved<InsertCheckProtosPass>();
		};
		private :
		// Set of checked DSNodes
		std::set<DSNode *> CheckedDSNodes;

		// The set of values that already have run-time checks
		std::set<Value *> CheckedValues;

		ArrayBoundsCheck * abcPass;
#ifndef  LLVA_KERNEL
		PoolAllocateGroup * paPass;
		EmbeCFreeRemoval *efPass;
		TargetData * TD;
#else
		TDDataStructures * TDPass;
#endif  
		Constant *RuntimeInit;
		Constant *PoolCheck;
		Constant *PoolCheckUI;
		Constant *PoolCheckArray;
		Constant *PoolCheckArrayUI;
		Constant *ExactCheck;
		Constant *ExactCheck2;
		Constant *FunctionCheck;
		Constant *GetActualValue;
		Constant *StackFree;
		void addPoolCheckProto(Module &M);
		void addPoolChecks(Module &M);
		void addGetElementPtrChecks(BasicBlock * BB);
		void addGetActualValue(llvm::ICmpInst*, unsigned int);
		bool insertExactCheck (GetElementPtrInst * GEP);
		bool insertExactCheck (Instruction * , Value *, Value *, Instruction *);
		DSNode* getDSNode(const Value *V, Function *F);
		unsigned getDSNodeOffset(const Value *V, Function *F);
		void registerStackObjects (Module &M);
		void registerAllocaInst(AllocaInst *AI, AllocaInst *AIOrig);
		void addExactCheck (Value * P, Value * I, Value * B, Instruction * InsertPt);
		void addExactCheck2 (Value * B, Value * R, Value * C, Instruction * InsertPt);
		DSGraph & getDSGraph (Function & F);
#ifndef LLVA_KERNEL  
		Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed = true);
		void registerGlobalArraysWithGlobalPools(Module &M);
#else
		Value * getPoolHandle(const Value *V, Function *F);
#endif  
	static bool isEligableForExactCheck (Value * Pointer, bool IOOkay);
		static Value * findSourcePointer (Value * PointerOperand, bool & indexed, bool IOOkay = true);

	};

	// TODO: Refactor the pass into a function / basic block pass
	struct AddLoadStoreCheckPass : public ModulePass {
		static char ID;
		AddLoadStoreCheckPass() : ModulePass((intptr_t) &ID) {};
		const char *getPassName() const { return "Inserting Load/Store checks"; }
		virtual bool runOnModule(Module &M);
		virtual void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<InsertPoolChecks>();
		};
#ifndef LLVA_KERNEL  
		void addLSChecks(Value *Vnew, const Value *V, Instruction *I, Function *F);
#else
		void addLSChecks(Value *V, Instruction *I, Function *F);
#endif  
		private:
		Constant *RuntimeInit;
		Constant *PoolCheck;
		Constant *PoolCheckUI;
		Constant *PoolCheckArray;
		Constant *PoolCheckArrayUI;
		Constant *ExactCheck;
		Constant *ExactCheck2;
		Constant *FunctionCheck;
		Constant *GetActualValue;
		Constant *StackFree;
			
		PoolAllocateGroup * paPass;
		TargetData * TD;
	
		InsertPoolChecks * m_PoolCheckPass;

		void addLoadStoreChecks(Module &M);
	};

	// Monotonic loop optimization
	struct MonotonicLoopOpt : public FunctionPass {
		friend struct InsertPoolChecks;
		public:
		static char ID;
		MonotonicLoopOpt() : FunctionPass((intptr_t) &ID),
		m_insertPoolChecks(NULL) {};
		const char *getPassName() const { return "Optimize checking for monotonic loops"; }
		virtual bool runOnFunction(Function &F);
		virtual void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<InsertPoolChecks>();
			AU.addRequired<ScalarEvolution>();
			AU.addRequired<LoopInfo>();
		};
		private:
		void addBoundChecks(DSNode * node, Value * PH, GetElementPtrInst * bound, BasicBlock::iterator & pos);
		void addGetElementPtrChecks (BasicBlock * BB);
		InsertPoolChecks * m_insertPoolChecks;
		ScalarEvolution  * scevPass;
		LoopInfo         * LI;
	};


}
#endif
