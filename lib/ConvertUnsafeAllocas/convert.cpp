//===- convert.cpp - Promote unsafe alloca instructions to heap allocations --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that promotes unsafe stack allocations to heap
// allocations.  It also updates the pointer analysis results accordingly.
//
// This pass relies upon the abcpre, abc, and checkstack safety passes.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "convalloca"

#include "safecode/Config/config.h"
#include "ConvertUnsafeAllocas.h"
#include "SCUtils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Instruction.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/InstVisitor.h"

#include <iostream>

using namespace llvm;

NAMESPACE_SC_BEGIN

using namespace CUA;

//
// Command line options
//
cl::opt<bool> DisableStackPromote ("disable-stackpromote", cl::Hidden,
                                   cl::init(false),
                                   cl::desc("Do not promote stack allocations"));
                                                                                

//
// Statistics
//
namespace {
  STATISTIC (ConvAllocas,  "Number of converted allocas");
  STATISTIC (MissingFrees, "Number of frees that we didn't insert");

  RegisterPass<ConvertUnsafeAllocas> cua
  ("convalloca", "Converts Unsafe Allocas");

  RegisterPass<PAConvertUnsafeAllocas> pacua
  ("paconvalloca", "Converts Unsafe Allocas using Pool Allocation Run-Time");
}

char CUA::ConvertUnsafeAllocas::ID = 0;
char CUA::PAConvertUnsafeAllocas::ID = 0;
char InitAllocas::ID = 0;

// Function pointers
static Constant * StackAlloc;
static Constant * NewStack;
static Constant * DelStack;

static void
createProtos (Module & M) {
#ifdef LLVA_KERNEL
  //
  // Get a reference to the sp_malloc() function (a function in the kernel
  // used for allocating promoted stack allocations).
  //
  std::vector<const Type *> Arg(1, Type::Int32Ty);
  FunctionType *kmallocTy = FunctionType::get(VoidPtrTy, Arg, false);
  kmalloc = M.getOrInsertFunction("sp_malloc", kmallocTy);

  //
  // If we fail to get the kmalloc function, generate an error.
  //
  assert ((kmalloc != 0) && "No kmalloc function found!\n");
#endif
}

bool
ConvertUnsafeAllocas::runOnModule (Module &M) {
  //
  // Retrieve all pre-requisite analysis results from other passes.
  //
  budsPass = &getAnalysis<EQTDDataStructures>();
  cssPass = &getAnalysis<checkStackSafety>();
  abcPass = &getAnalysis<ArrayBoundsCheckGroup>();
  TD = &getAnalysis<TargetData>();

  //
  // Add prototype for run-time functions.
  //
  createProtos(M);

  unsafeAllocaNodes.clear();
  getUnsafeAllocsFromABC(M);
  if (!DisableStackPromote)
    TransformCSSAllocasToMallocs (M, cssPass->AllocaNodes);
#ifndef LLVA_KERNEL
#if 0
  TransformAllocasToMallocs(unsafeAllocaNodes);
  TransformCollapsedAllocas(M);
#endif
#endif
  return true;
}

bool ConvertUnsafeAllocas::markReachableAllocas(DSNode *DSN) {
  reachableAllocaNodes.clear();
  return markReachableAllocasInt(DSN);
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

//
// Method: InsertFreesAtEnd()
//
// Description:
//  Insert free instructions so that the memory allocated by the specified
//  malloc instruction is freed on function exit.
//
void
ConvertUnsafeAllocas::InsertFreesAtEnd(MallocInst *MI) {
  assert (MI && "MI is NULL!\n");

  //
  // Get the dominance frontier information about the malloc instruction's
  // basic block.  We cache the information in case we end up processing
  // multiple instructions from the same function.
  //
  BasicBlock *currentBlock = MI->getParent();
  Function * F = currentBlock->getParent();
  DominanceFrontier * dfmt = &getAnalysis<DominanceFrontier>(*F);
  DominatorTree     * domTree = &getAnalysis<DominatorTree>(*F);

  DominanceFrontier::const_iterator it = dfmt->find(currentBlock);

#if 0
  //
  // If the basic block has a dominance frontier, use it.
  //
  if (it != dfmt->end()) {
    const DominanceFrontier::DomSetType &S = it->second;
    if (S.size() > 0) {
      DominanceFrontier::DomSetType::iterator pCurrent = S.begin(),
                                                  pEnd = S.end();
      for (; pCurrent != pEnd; ++pCurrent) {
        BasicBlock *frontierBlock = *pCurrent;
        // One of its predecessors is dominated by currentBlock;
        // need to insert a free in that predecessor
        for (pred_iterator SI = pred_begin(frontierBlock),
                           SE = pred_end(frontierBlock);
                           SI != SE; ++SI) {
          BasicBlock *predecessorBlock = *SI;
          if (domTree->dominates (predecessorBlock, currentBlock)) {
            // Get the terminator
            Instruction *InsertPt = predecessorBlock->getTerminator();
            new FreeInst(MI, InsertPt);
          } 
        }
      }

      return;
    }
  }
#endif

  //
  // There is no dominance frontier; insert frees on all returns;
  //
  std::vector<Instruction*> FreePoints;
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
    if (isa<ReturnInst>(BB->getTerminator()) ||
        isa<UnwindInst>(BB->getTerminator()))
      FreePoints.push_back(BB->getTerminator());

  //
  // We have the Free points; now we construct the free instructions at each
  // of the points.
  //
  std::vector<Instruction*>::iterator fpI = FreePoints.begin(),
                                      fpE = FreePoints.end();
  for (; fpI != fpE ; ++ fpI) {
    //
    // Determine whether the allocation dominates the return.  If not, then
    // don't insert a free instruction for now.
    //
    Instruction *InsertPt = *fpI;
    if (domTree->dominates (MI->getParent(), InsertPt->getParent())) {
      new FreeInst(MI, InsertPt);
    } else {
      ++MissingFrees;
    }
  }
}

// Precondition: Enforce that the alloca nodes haven't been already converted
void ConvertUnsafeAllocas::TransformAllocasToMallocs(std::list<DSNode *> 
						     & unsafeAllocaNodes) {

  std::list<DSNode *>::const_iterator iCurrent = unsafeAllocaNodes.begin(), 
                                      iEnd     = unsafeAllocaNodes.end();

  for (; iCurrent != iEnd; ++iCurrent) {
    DSNode *DSN = *iCurrent;
    
    // Now change the alloca instruction corresponding to the node	
    // to malloc 
    DSGraph *DSG = DSN->getParentGraph();
    DSGraph::ScalarMapTy &SM = DSG->getScalarMap();

#ifndef LLVA_KERNEL    
    MallocInst *MI = 0;
#else
    Value *MI = 0;
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
        if (AllocaInst *AI = dyn_cast<AllocaInst>((Value *)(SMI->first))) {
          //create a new malloc instruction
          if (AI->getParent() != 0) {
#ifndef LLVA_KERNEL	  
            MI = new MallocInst(AI->getType()->getElementType(),
                                AI->getArraySize(), AI->getName(), AI);
#else
            Value *AllocSize =
            ConstantInt::get(Type::Int32Ty,
                              TD->getTypeAllocSize(AI->getAllocatedType()));
	    
            if (AI->isArrayAllocation())
              AllocSize = BinaryOperator::Create(Instruction::Mul, AllocSize,
                                                 AI->getOperand(0), "sizetmp",
                                                 AI);	    
            std::vector<Value *> args(1, AllocSize);
            CallInst *CI = new CallInst(kmalloc, args.begin(), args.end(), "", AI);
            MI = castTo (CI, AI->getType(), "", AI);
#endif	    
            DSN->setHeapMarker();
            AI->replaceAllUsesWith(MI);
            SM.erase(SMI++);
            AI->getParent()->getInstList().erase(AI);
            ++ConvAllocas;
	    	    InsertFreesAtEnd(MI);
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

//
// Method: TransformCSSAllocasToMallocs()
//
// Description:
//  This method is given the set of DSNodes from the stack safety pass that
//  have been marked for promotion.  It then finds all alloca instructions
//  that have not been marked type-unknown and promotes them to heap
//  allocations.
//
void
ConvertUnsafeAllocas::TransformCSSAllocasToMallocs (Module & M,
                                                    std::set<DSNode *> & cssAllocaNodes) {
  for (Module::iterator FI = M.begin(); FI != M.end(); ++FI) {
    //
    // Skip functions that have no DSGraph.  These are probably functions with
    // no function body and are, hence, cannot be analyzed.
    //
    if (!(budsPass->hasDSGraph (*FI))) continue;

    //
    // Get the DSGraph for the current function.
    //
    DSGraph *DSG = budsPass->getDSGraph(*FI);

    //
    // Search for alloca instructions that need promotion and add them to the
    // worklist.
    //
    std::vector<AllocaInst *> Worklist;
    for (Function::iterator BB = FI->begin(); BB != FI->end(); ++BB) {
      for (BasicBlock::iterator ii = BB->begin(); ii != BB->end(); ++ii) {
        Instruction * I = ii;

        if (AllocaInst * AI = dyn_cast<AllocaInst>(I)) {
          //
          // Get the DSNode for the allocation.
          //
          DSNode *DSN = DSG->getNodeForValue(AI).getNode();
          assert (DSN && "No DSNode for alloca!\n");

          //
          // If the alloca is type-known, we do not need to promote it, so
          // don't bother with it.
          //
          if (DSN->isNodeCompletelyFolded()) continue;

          //
          // Determine if the DSNode for the alloca is one of those marked as
          // unsafe by the stack safety analysis pass.  If not, then we do not
          // need to promote it.
          //
          if (cssAllocaNodes.find(DSN) == cssAllocaNodes.end()) continue;

          //
          // If the DSNode for this alloca is already listed in the
          // unsafeAllocaNode vector, remove it since we are processing it here
          //
          std::list<DSNode *>::iterator NodeI = find (unsafeAllocaNodes.begin(),
                                                      unsafeAllocaNodes.end(),
                                                      DSN);
          if (NodeI != unsafeAllocaNodes.end()) {
            unsafeAllocaNodes.erase(NodeI);
          }

          //
          // This alloca needs to be changed to a malloc.  Add it to the
          // worklist.
          //
          Worklist.push_back (AI);
        }
      }
    }

    //
    // Get the dominator information for the current function.
    //
    DominanceFrontier & dfmt    = getAnalysis<DominanceFrontier>(*FI);
    DominatorTree     & domTree = getAnalysis<DominatorTree>(*FI);

    //
    // Update the statistics.
    //
    ConvAllocas += Worklist.size();

    //
    // Convert everything in the worklist into a malloc instruction.
    //
    while (Worklist.size()) {
      //
      // Grab an alloca from the worklist.
      //
      AllocaInst * AI = Worklist.back();
      Worklist.pop_back();

      //
      // Get the DSNode for this alloca.
      //
      DSNode *DSN = DSG->getNodeForValue(AI).getNode();
      assert (DSN && "No DSNode for alloca!\n");

      //
      // Promote the alloca and remove it from the program.
      //
      Value * MI = promoteAlloca (AI, DSN);
      AI->getParent()->getInstList().erase(AI);
    }
  }
}

DSNode * ConvertUnsafeAllocas::getDSNode(const Value *V, Function *F) {
  DSGraph * TDG = budsPass->getDSGraph(*F);
  DSNode *DSN = TDG->getNodeForValue((Value *)V).getNode();
  return DSN;
}

DSNode * ConvertUnsafeAllocas::getTDDSNode(const Value *V, Function *F) {
#if 0
  DSGraph &TDG = tddsPass->getDSGraph(*F);
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
#else
  return 0;
#endif
}

//
// Method: promoteAlloca()
//
// Description:
//  Rewrite the given alloca instruction into an instruction that performs a
//  heap allocation of the same size.
//
// Inputs:
//  AI      - The alloca instruction to promote.
//  Node    - The DSNode of the alloca.
//
Value *
ConvertUnsafeAllocas::promoteAlloca (AllocaInst * AI, DSNode * Node) {
#ifndef LLVA_KERNEL
  MallocInst *MI = new MallocInst(AI->getType()->getElementType(),
                                  AI->getArraySize(), AI->getName(), 
                                  AI);
  InsertFreesAtEnd (MI);
#else
  Value *AllocSize;
  AllocSize = ConstantInt::get (Type::Int32Ty,
                                TD->getTypeAllocSize(AI->getAllocatedType()));
  if (AI->isArrayAllocation())
    AllocSize = BinaryOperator::Create (Instruction::Mul, AllocSize,
                                        AI->getOperand(0), "sizetmp",
                                        AI);	    

  std::vector<Value *> args (1, AllocSize);
  CallInst *CI = new CallInst (kmalloc, args.begin(), args.end(), "", AI);
  Value * MI = castTo (CI, AI->getType(), "", AI);
#endif

  //
  // Update the pointer analysis to know that pointers to this object can now
  // point to heap objects.
  //
  Node->setHeapMarker();

  //
  // Replace all uses of the old alloca instruction with the new heap
  // allocation.
  AI->replaceAllUsesWith(MI);

  return MI;
}

//
// Method: TransformCollapsedAllocas()
//
// Description:
//  Transform all stack allocated objects that are type-unknown
//  (i.e., are completely folded) to heap allocations.
//
void
ConvertUnsafeAllocas::TransformCollapsedAllocas(Module &M) {
  //
  // Need to check if the following is incomplete because we are only looking
  // at scalars.
  //
  // It may be complete because every instruction actually is a scalar in
  // LLVM?!
  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) {
    if (!MI->isDeclaration()) {
      DSGraph *G = budsPass->getDSGraph(*MI);
      DSGraph::ScalarMapTy &SM = G->getScalarMap();
      for (DSGraph::ScalarMapTy::iterator SMI = SM.begin(), SME = SM.end();
           SMI != SME; ) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>((Value *)(SMI->first))) {
          if (SMI->second.getNode()->isNodeCompletelyFolded()) {
#ifndef LLVA_KERNEL
            MallocInst *MI = new MallocInst(AI->getType()->getElementType(),
                                            AI->getArraySize(), AI->getName(), 
                                            AI);
	    	    InsertFreesAtEnd(MI);
#else
            Value *AllocSize =
            ConstantInt::get(Type::Int32Ty,
                              TD->getTypeAllocSize(AI->getAllocatedType()));
            if (AI->isArrayAllocation())
              AllocSize = BinaryOperator::Create(Instruction::Mul, AllocSize,
                                                 AI->getOperand(0), "sizetmp",
                                                 AI);	    

            std::vector<Value *> args(1, AllocSize);
            CallInst *CI = new CallInst(kmalloc, args.begin(), args.end(), "", AI);
            Value * MI = castTo (CI, AI->getType(), "", AI);
#endif
            AI->replaceAllUsesWith(MI);
            SMI->second.getNode()->setHeapMarker();
            SM.erase(SMI++);
            AI->getParent()->getInstList().erase(AI);	  
            ++ConvAllocas;
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
    
// Helper class to build UnsafeAllocaNodeList
class UnsafeAllocaNodeListBuilder : public InstVisitor<UnsafeAllocaNodeListBuilder> {
  public:
    UnsafeAllocaNodeListBuilder(EQTDDataStructures * budsPass, std::list<DSNode *> & unsafeAllocaNodes) : 
      budsPass(budsPass), unsafeAllocaNodes(unsafeAllocaNodes) {}
    void visitGetElementPtrInst(GetElementPtrInst &GEP) {
      Value *pointerOperand = GEP.getPointerOperand();
      DSGraph * TDG = budsPass->getDSGraph(*(GEP.getParent()->getParent()));
      DSNode *DSN = TDG->getNodeForValue(pointerOperand).getNode();
      //FIXME DO we really need this ?	    markReachableAllocas(DSN);
      if (DSN && DSN->isAllocaNode() && !DSN->isNodeCompletelyFolded()) {
        unsafeAllocaNodes.push_back(DSN);
      }
    }
  private:
    EQTDDataStructures * budsPass;
    std::list<DSNode *> & unsafeAllocaNodes;
};

//
// Method: getUnsafeAllocsFromABC()
//
// Description:
//  Find all memory objects that are both allocated on the stack and are not
//  proven to be indexed in a type-safe manner according to the static array
//  bounds checking pass.
//
// Notes:
//  This method saves its results be remembering the set of DSNodes which are
//  both on the stack and potentially indexed in a type-unsafe manner.
//
// FIXME:
//  This method only considers unsafe GEP instructions; it does not consider
//  unsafe call instructions or other instructions deemed unsafe by the array
//  bounds checking pass.
//
void
ConvertUnsafeAllocas::getUnsafeAllocsFromABC(Module & M) {
  UnsafeAllocaNodeListBuilder Builder(budsPass, unsafeAllocaNodes);
  Builder.visit(M);
#if 0
  // Haohui: Disable it right now since nobody using the code

  std::map<BasicBlock *,std::set<Instruction*>*> UnsafeGEPMap= abcPass->UnsafeGetElemPtrs;
  std::map<BasicBlock *,std::set<Instruction*>*>::const_iterator bCurrent = UnsafeGEPMap.begin(), bEnd = UnsafeGEPMap.end();
  for (; bCurrent != bEnd; ++bCurrent) {
    std::set<Instruction *> * UnsafeGetElemPtrs = bCurrent->second;
    std::set<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs->begin(), iEnd = UnsafeGetElemPtrs->end();
    for (; iCurrent != iEnd; ++iCurrent) {
      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(*iCurrent)) {
        Value *pointerOperand = GEP->getPointerOperand();
        DSGraph * TDG = budsPass->getDSGraph(*(GEP->getParent()->getParent()));
        DSNode *DSN = TDG->getNodeForValue(pointerOperand).getNode();
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
#endif
}

//=============================================================================
// Methods for Promoting Stack Allocations to Pool Allocation Heap Allocations
//=============================================================================

//
// Method: InsertFreesAtEnd()
//
// Description:
//  Insert a call on all return paths from the function so that stack memory
//  that has been promoted to the heap is all deallocated in one fell swoop.
//
void
PAConvertUnsafeAllocas::InsertFreesAtEndNew (Value * PH, Instruction * MI) {
  assert (MI && "MI is NULL!\n");

  BasicBlock *currentBlock = MI->getParent();
  Function * F = currentBlock->getParent();

  //
  // Insert a call to the pool allocation free function on all return paths.
  //
  std::vector<Instruction*> FreePoints;
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
    if (isa<ReturnInst>(BB->getTerminator()) ||
        isa<UnwindInst>(BB->getTerminator()))
      FreePoints.push_back(BB->getTerminator());

  // We have the Free points; now we construct the free instructions at each
  // of the points.
  std::vector<Instruction*>::iterator fpI = FreePoints.begin(),
                                      fpE = FreePoints.end();
  for (; fpI != fpE ; ++ fpI) {
    Instruction *InsertPt = *fpI;
    std::vector<Value *> args;
    args.push_back (PH);
    CallInst::Create (DelStack, args.begin(), args.end(), "", InsertPt);
  }
}

//
// Method: promoteAlloca()
//
// Description:
//  Rewrite the given alloca instruction into an instruction that performs a
//  heap allocation of the same size.
//
static std::set<Function *> FuncsWithPromotes;
Value *
PAConvertUnsafeAllocas::promoteAlloca (AllocaInst * AI, DSNode * Node) {
  // Function in which the allocation lives
  Function * F = AI->getParent()->getParent();

  //
  // If this function is a clone, get the original function for looking up
  // information.
  //
  if (!(paPass->getFuncInfo(*F))) {
    F = paPass->getOrigFunctionFromClone(F);
    assert (F && "No Function Information from Pool Allocation!\n");
  }

  //
  // Create the size argument to the allocation.
  //
  Value *AllocSize;
  AllocSize = ConstantInt::get (Type::Int32Ty,
                                TD->getTypeAllocSize(AI->getAllocatedType()));
  if (AI->isArrayAllocation())
    AllocSize = BinaryOperator::Create (Instruction::Mul, AllocSize,
                                        AI->getOperand(0), "sizetmp",
                                        AI);	    

  //
  // Get the pool associated with the alloca instruction.
  //
  Value * PH = paPass->getPool (Node, *(AI->getParent()->getParent()));
  assert (PH && "No pool handle for this stack node!\n");

  //
  // Create the call to the pool allocation function.
  //
  std::vector<Value *> args;
  args.push_back (PH);
  args.push_back (AllocSize);
  CallInst *CI = CallInst::Create (StackAlloc, args.begin(), args.end(), "", AI);
  Instruction * MI = castTo (CI, AI->getType(), "", AI);

  //
  // Update the pointer analysis to know that pointers to this object can now
  // point to heap objects.
  //
  Node->setHeapMarker();

  //
  // Replace all uses of the old alloca instruction with the new heap
  // allocation.
  AI->replaceAllUsesWith(MI);

  //
  // Add prolog and epilog code to the function as appropriate.
  //
  if (FuncsWithPromotes.find (F) == FuncsWithPromotes.end()) {
    std::vector<Value *> args;
    args.push_back (PH);
    CallInst::Create (NewStack, args.begin(), args.end(), "", F->begin()->begin());
    InsertFreesAtEndNew (PH, MI);
    FuncsWithPromotes.insert (F);
  }
  return MI;
}


bool
PAConvertUnsafeAllocas::runOnModule (Module &M) {
  //
  // Retrieve all pre-requisite analysis results from other passes.
  //
  TD       = &getAnalysis<TargetData>();
  budsPass = &getAnalysis<EQTDDataStructures>();
  cssPass  = &getAnalysis<checkStackSafety>();
  abcPass  = &getAnalysis<ArrayBoundsCheckGroup>();
  paPass   =  getAnalysisIfAvailable<PoolAllocateGroup>();
  assert (paPass && "Pool Allocation Transform *must* be run first!");

  //
  // Add prototype for run-time functions.
  //
  createProtos(M);

  //
  // Get references to the additional functions used for pool allocating stack
  // allocations.
  //
  Type * VoidPtrTy = PointerType::getUnqual(Type::Int8Ty);
  std::vector<const Type *> Arg;
  Arg.push_back (PointerType::getUnqual(paPass->getPoolType()));
  Arg.push_back (Type::Int32Ty);
  FunctionType * FuncTy = FunctionType::get (VoidPtrTy, Arg, false);
  StackAlloc = M.getOrInsertFunction ("pool_alloca", FuncTy);

  Arg.clear();
  Arg.push_back (PointerType::getUnqual(paPass->getPoolType()));
  FuncTy = FunctionType::get (Type::VoidTy, Arg, false);
  NewStack = M.getOrInsertFunction ("pool_newstack", FuncTy);
  DelStack = M.getOrInsertFunction ("pool_delstack", FuncTy);

  unsafeAllocaNodes.clear();
  getUnsafeAllocsFromABC(M);
  if (!DisableStackPromote)
    TransformCSSAllocasToMallocs(M, cssPass->AllocaNodes);
#ifndef LLVA_KERNEL
#if 0
  TransformAllocasToMallocs(unsafeAllocaNodes);
  TransformCollapsedAllocas(M);
#endif
#endif

  return true;
}

NAMESPACE_SC_END
