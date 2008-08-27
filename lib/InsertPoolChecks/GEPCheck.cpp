/**
 * $Id: GEPCheck.cpp,v 1.1 2008-08-27 05:37:31 mai4 Exp $
 **/

#include <iostream>
#include "safecode/Config/config.h"
#include "SCUtils.h"
#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

char llvm::MonotonicLoopOpt::ID = 0;

static RegisterPass<MonotonicLoopOpt> X("monotonic-loop-opt", "Optimize checking for monotonic loops");

// Pass Statistics
namespace {
	//MonotonicOpts
	STATISTIC (MonotonicOpts,       "Number of monotonic LICM bounds check optimisations");
}

////////////////////////////////////////////////////////////////////////////
// MonotonicLoopOpt Methods
////////////////////////////////////////////////////////////////////////////
namespace llvm {
	// Helper function: add bounds checking instructions
	void MonotonicLoopOpt::addBoundChecks(DSNode * node, Value * PH, GetElementPtrInst * GEP, BasicBlock::iterator & pos)
	{
		Value *	Casted = castTo (GEP,
							PointerType::getUnqual(Type::Int8Ty),
							(GEP)->getName()+".pc.casted",
							pos);

	  Value * CastedSrc = castTo (GEP->getPointerOperand(),
							PointerType::getUnqual(Type::Int8Ty),
							(Casted)->getName()+".pcsrc.casted",
							pos);

		Value *CastedPH = castTo (PH,
							PointerType::getUnqual(Type::Int8Ty),
							"jtcph",
							pos);

		std::vector<Value *> args(1, CastedPH);
		args.push_back(CastedSrc);
		args.push_back(Casted);

		CallInst::Create(node->isIncompleteNode() ? m_insertPoolChecks->PoolCheckArrayUI : m_insertPoolChecks->PoolCheckArray, args.begin(), args.end(), "", pos);
	}

	bool MonotonicLoopOpt::runOnFunction(Function &F) {
		LI        = &getAnalysis<LoopInfo>();
		scevPass  = &getAnalysis<ScalarEvolution>();
		m_insertPoolChecks = &getAnalysis<InsertPoolChecks>();
		Function::iterator fI = F.begin(), fE = F.end();
		for ( ; fI != fE; ++fI) {
			BasicBlock * BB = fI;
			addGetElementPtrChecks (BB);
//		std::cerr << BB->getName() << std::endl;
		}
		return true;
	}

	void MonotonicLoopOpt::addGetElementPtrChecks (BasicBlock * BB) {
			std::set<Instruction *> * UnsafeGetElemPtrs = m_insertPoolChecks->abcPass->getUnsafeGEPs (BB);
			if (!UnsafeGetElemPtrs)
				return;
			std::set<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs->begin(), iEnd = UnsafeGetElemPtrs->end();
			for (; iCurrent != iEnd; ++iCurrent) {
				if (!isa<GetElementPtrInst>(*iCurrent)) continue;
				// We have the GetElementPtr
				GetElementPtrInst *GEP = cast<GetElementPtrInst>(*iCurrent);
				Function *F = GEP->getParent()->getParent();
				PA::FuncInfo *FI = m_insertPoolChecks->paPass->getFuncInfoOrClone(*F);
				Value *PH = m_insertPoolChecks->getPoolHandle(GEP, F, *FI);
				if (PH && isa<ConstantPointerNull>(PH)) continue;

				// Now check if the GEP is inside a loop with monotonically increasing
				//loop bounds
				//We use the LoopInfo Pass this
				bool monotonicOpt = false;
				Loop *L = LI->getLoopFor(BB);
				if (L && (GEP->getNumOperands() == 3)) {
					bool HasConstantItCount = isa<SCEVConstant>(scevPass->getIterationCount(L));
					Value *vIndex = GEP->getOperand(2);
					Instruction *Index = dyn_cast<Instruction>(vIndex);
					if (Index && (L->isLoopInvariant(GEP->getPointerOperand()))) {
						//If it is not an instruction then it must already be loop invariant
						SCEVHandle SH = scevPass->getSCEV(Index);
						if (HasConstantItCount || SH->hasComputableLoopEvolution(L)) {
							// Varies predictably
							if (SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SH))
								if (AR->isAffine()) {
									SCEVHandle EntryValue = AR->getStart();
									SCEVHandle ExitValue = scevPass->getSCEVAtScope(Index, L->getParentLoop());
									BasicBlock *Preheader = L->getLoopPreheader();
									if ((Preheader) && (!isa<SCEVCouldNotCompute>(ExitValue)) && (!isa<SCEVCouldNotCompute>(EntryValue))) {
										monotonicOpt = true;
										++MonotonicOpts;
										SCEVExpander Rewriter(*scevPass, *LI);
										Instruction *ptIns = Preheader->getTerminator();

										Value *UpperBound = Rewriter.expandCodeFor(ExitValue, ptIns);
										Value *LowerBound = Rewriter.expandCodeFor(EntryValue, ptIns);
										//Inserted the values now insert GEPs and add checks

										//
										// Insert a bounds check and use its return value in all subsequent
										// uses.
										//
										BasicBlock::iterator InsertPt = Preheader->getTerminator();
										// Insert it
										DSNode * Node = m_insertPoolChecks->getDSNode (GEP, F);
										m_insertPoolChecks->CheckedDSNodes.insert (Node);
										m_insertPoolChecks->CheckedValues.insert (GEP);

										std::vector<Value *> gep_upper_arg(1, GEP->getOperand(1));
										gep_upper_arg.push_back(UpperBound);

										GetElementPtrInst *GEPUpper =
											GetElementPtrInst::Create(GEP->getPointerOperand(), gep_upper_arg.begin(), gep_upper_arg.end(), GEP->getName()+".upbc", ptIns);
										addBoundChecks(Node, PH, GEPUpper, InsertPt);

										std::vector<Value *> gep_lower_arg(1, GEP->getOperand(1));
										gep_lower_arg.push_back(LowerBound);
										GetElementPtrInst *GEPLower =
											GetElementPtrInst::Create(GEP->getPointerOperand(), gep_lower_arg.begin(), gep_lower_arg.end(), GEP->getName()+".lobc", ptIns);

										addBoundChecks(Node, PH, GEPLower, InsertPt);
										DEBUG(std::cerr << "inserted instrcution with monotonic optimization\n");
									}
								}
						}
					}
				}

				if (!monotonicOpt)
				{
					// Normal version
					BasicBlock::iterator InsertPt = GEP;
					++InsertPt;

					// Insert it
					DSNode * Node = m_insertPoolChecks->getDSNode (GEP, F);
					m_insertPoolChecks->CheckedDSNodes.insert (Node);
					m_insertPoolChecks->CheckedValues.insert (GEP);

					addBoundChecks(Node, PH, GEP, InsertPt);
				}
			}
		}
}
