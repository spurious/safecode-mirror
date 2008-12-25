//===- RegisterStackObjPass.cpp - Pass to Insert Stack Object Registration ---//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass instruments code to register stack objects with the appropriate
// pool.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "stackreg"

#include "safecode/SAFECode.h"

#include "SCUtils.h"
#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "safecode/VectorListHelper.h"

NAMESPACE_SC_BEGIN

char RegisterStackObjPass::ID = 0;

static RegisterPass<RegisterStackObjPass> passRegStackObj ("reg-stack-obj", "register stack objects into pools");

// Pass Statistics
namespace {
  // Object registration statistics
  STATISTIC (StackRegisters,      "Stack registrations");
  STATISTIC (SavedRegAllocs,      "Stack registrations avoided");
}

////////////////////////////////////////////////////////////////////////////
// RegisterStackObjPass Methods
////////////////////////////////////////////////////////////////////////////
 
  // Prototypes of the poolunregister function
  static Constant * StackFree;

  bool
  RegisterStackObjPass::doInitialization(Module & M) {
    static const Type * vpTy = PointerType::getUnqual(Type::Int8Ty);
    FunctionType *StackFreeTy = FunctionType::get(vpTy, args<const Type*>::list(vpTy, vpTy), false);
    StackFree = M.getOrInsertFunction("poolunregister", StackFreeTy);
    return false;
  }

  bool
  RegisterStackObjPass::runOnFunction(Function & F) {
    // paPass = getAnalysisToUpdate<PoolAllocateGroup>();
    TD = &getAnalysis<TargetData>();
    paPass = &getAnalysis<PoolAllocateGroup>();
    dsnPass = &getAnalysis<DSNodePass>();
    DT = &getAnalysis<DominatorTree>();
    for (Function::iterator BI = F.begin(); BI != F.end(); ++BI) {
      for (BasicBlock::iterator I = BI->begin(); I != BI->end(); ++I) {
      DomTreeNode * DTN = DT->getNode(BI);
        if (AllocaInst * AI = dyn_cast<AllocaInst>(I)) {
          registerAllocaInst (AI, AI, DTN);
        }
      }
    }
    return true;
  }

  void
  RegisterStackObjPass::registerAllocaInst(AllocaInst *AI,
                                           AllocaInst *AIOrig,
                                           DomTreeNode * DTN) {
    //
    // Get the function information for this function.
    //
    Function *F = AI->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    Value *temp = FI->MapValueToOriginal(AI);
    if (temp)
      AIOrig = dyn_cast<AllocaInst>(temp);

    //
    // Get the pool handle for the node that this contributes to...
    //
    Function *FOrig  = AIOrig->getParent()->getParent();
    DSNode *Node = dsnPass->getDSNode(AIOrig, FOrig);
    if (!Node) return;
    assert ((Node->isAllocaNode()) && "DSNode for alloca is missing stack flag!");

    //
    // Only register the stack allocation if it may be the subject of a
    // run-time check.  This can only occur when the object is used like an
    // array because:
    //  1) GEP checks are only done when accessing arrays.
    //  2) Load/Store checks are only done on collapsed nodes (which appear to
    //     be used like arrays).
    //
#if 0
    if (!(Node->isArray()))
      return;
#endif

    //
    // Determine if we have ever done a check on this alloca or a pointer
    // aliasing this alloca.  If not, then we can forego the check (even if we
    // can't trace through all the data flow).
    //
    // FIXME:
    //  This implementation is incorrect.  A node in the DSGraph will have
    //  different DSNodes in different functions (because each function has its
    //  own copy of the DSGraph).  We will need to find another way to do this
    //  optimization.
    //
    if (dsnPass->isDSNodeChecked(Node)) {
      ++SavedRegAllocs;
      return;
    }

    //
    // Determine if any use (direct or indirect) escapes this function.  If
    // not, then none of the checks will consult the MetaPool, and we can
    // forego registering the alloca.
    //
    bool MustRegisterAlloca = false;
    std::vector<Value *> AllocaWorkList;
    AllocaWorkList.push_back (AI);
    while ((!MustRegisterAlloca) && (AllocaWorkList.size())) {
      Value * V = AllocaWorkList.back();
      AllocaWorkList.pop_back();
      Value::use_iterator UI = V->use_begin();
      for (; UI != V->use_end(); ++UI) {
        // We cannot handle PHI nodes or Select instructions
        if (isa<PHINode>(UI) || isa<SelectInst>(UI)) {
          MustRegisterAlloca = true;
          continue;
        }

        // The pointer escapes if it's stored to memory somewhere.
        StoreInst * SI;
        if ((SI = dyn_cast<StoreInst>(UI)) && (SI->getOperand(0) == V)) {
          MustRegisterAlloca = true;
          continue;
        }

        // GEP instructions are okay, but need to be added to the worklist
        if (isa<GetElementPtrInst>(UI)) {
          AllocaWorkList.push_back (*UI);
          continue;
        }

        // Cast instructions are okay as long as they cast to another pointer
        // type
        if (CastInst * CI = dyn_cast<CastInst>(UI)) {
          if (isa<PointerType>(CI->getType())) {
            AllocaWorkList.push_back (*UI);
            continue;
          } else {
            MustRegisterAlloca = true;
            continue;
          }
        }

  #if 0
        if (ConstantExpr *cExpr = dyn_cast<ConstantExpr>(UI)) {
          if (cExpr->getOpcode() == Instruction::Cast) {
            AllocaWorkList.push_back (*UI);
            continue;
          } else {
            MustRegisterAlloca = true;
            continue;
          }
        }
  #endif

        CallInst * CI1;
        if ((CI1 = dyn_cast<CallInst>(UI))) {
          if (!(CI1->getCalledFunction())) {
            MustRegisterAlloca = true;
            continue;
          }

          std::string FuncName = CI1->getCalledFunction()->getName();
          if (FuncName == "exactcheck3") {
            AllocaWorkList.push_back (*UI);
            continue;
          } else if ((FuncName == "llvm.memcpy.i32")    || 
                     (FuncName == "llvm.memcpy.i64")    ||
                     (FuncName == "llvm.memset.i32")    ||
                     (FuncName == "llvm.memset.i64")    ||
                     (FuncName == "llvm.memmove.i32")   ||
                     (FuncName == "llvm.memmove.i64")   ||
                     (FuncName == "llva_memcpy")        ||
                     (FuncName == "llva_memset")        ||
                     (FuncName == "llva_strncpy")       ||
                     (FuncName == "llva_invokememcpy")  ||
                     (FuncName == "llva_invokestrncpy") ||
                     (FuncName == "llva_invokememset")  ||
                     (FuncName == "memcmp")) {
            continue;
          } else {
            MustRegisterAlloca = true;
            continue;
          }
        }
      }
    }

    if (!MustRegisterAlloca) {
      ++SavedRegAllocs;
      return;
    }

    //
    // Insert the alloca registration.
    //
    Value *PH = dsnPass->getPoolHandle(AIOrig, FOrig, *FI);
    if (PH == 0 || isa<ConstantPointerNull>(PH)) return;

    Value *AllocSize =
      ConstantInt::get(Type::Int32Ty, TD->getABITypeSize(AI->getAllocatedType()));
    
    if (AI->isArrayAllocation())
      AllocSize = BinaryOperator::Create(Instruction::Mul, AllocSize,
                                         AI->getOperand(0), "sizetmp", AI);

    // Insert object registration at the end of allocas.
    Instruction *iptI = AI;
    BasicBlock::iterator InsertPt = AI;
    iptI = ++InsertPt;
    if (AI->getParent() == (&(AI->getParent()->getParent()->getEntryBlock()))) {
      InsertPt = AI->getParent()->begin();
      while (&(*(InsertPt)) != AI)
        ++InsertPt;
      while (isa<AllocaInst>(InsertPt))
        ++InsertPt;
      iptI = InsertPt;
    }

    //
    // Insert a call to register the object.
    //
    Instruction *Casted = castTo (AI, PointerType::getUnqual(Type::Int8Ty),
                                  AI->getName()+".casted", iptI);
    Value * CastedPH = PH;
    std::vector<Value *> args;
    args.push_back (CastedPH);
    args.push_back (Casted);
    args.push_back (AllocSize);
    Constant *PoolRegister = paPass->PoolRegister;
    CallInst::Create (PoolRegister, args.begin(), args.end(), "", iptI);

    //
    // Insert a call to unregister the object whenever the function can exit.
    //
    // FIXME:
    //  While the code below fixes some test cases, it is still incomplete. We
    //  may have a basic block that does not dominate *any* basic block that
    //  ends with a ret or unwind instruction.  What the code should do (I
    //  think) is:
    //    a) Insert poolunregister() calls before all return/unwind
    //       instructions dominated by the alloca's basic block
    //    b) Use the dominance frontier to find other basic blocks which should
    //       also call poolunregister() to unregister the alloca instruction.
    //
    //  Also, there may be even more issues with alloca's inside of loops.
    //
    CastedPH=castTo(PH,PointerType::getUnqual(Type::Int8Ty),"allocph",Casted);
    args.clear();
    args.push_back (CastedPH);
    args.push_back (Casted);
    const std::vector<DomTreeNode*> &children = DTN->getChildren();
    for (unsigned int i = 0; i < children.size(); ++i) {
      iptI = children[i]->getBlock()->getTerminator();
      if (isa<ReturnInst>(iptI) || isa<UnwindInst>(iptI))
        CallInst::Create (StackFree, args.begin(), args.end(), "", iptI);
    }

    // Update statistics
    ++StackRegisters;
  }

NAMESPACE_SC_END
