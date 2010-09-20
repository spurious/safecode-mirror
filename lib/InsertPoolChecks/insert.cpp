//===- insert.cpp - Insert run-time checks -------------------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass instruments a program with the necessary run-time checks for
// SAFECode.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "safecode"

#include "safecode/SAFECode.h"

#include <iostream>

#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

#include "SCUtils.h"
#include "InsertPoolChecks.h"
#include "safecode/VectorListHelper.h"

NAMESPACE_SC_BEGIN

char InsertPoolChecks::ID = 0;

static RegisterPass<InsertPoolChecks> ipcPass ("safecode", "insert runtime checks");

// Options for Enabling/Disabling the Insertion of Various Checks
cl::opt<bool> EnableIncompleteChecks  ("enable-incompletechecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Enable Checks on Incomplete Nodes"));

cl::opt<bool> EnableNullChecks  ("enable-nullchecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Enable Checks on NULL Pools"));


cl::opt<bool> DisableLSChecks  ("disable-lschecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable Load/Store Checks"));

cl::opt<bool> DisableGEPChecks ("disable-gepchecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable GetElementPtr(GEP) Checks"));

cl::opt<bool> DisableIntrinsicChecks ("disable-intrinchecks", cl::Hidden,
                                      cl::init(false),
                                      cl::desc("Disable Intrinsic Checks"));

// Options for where to insert various initialization code
cl::opt<string> InitFunctionName ("initfunc",
                                  cl::desc("Specify name of initialization "
                                           "function"),
                                  cl::value_desc("function name"));

// Pass Statistics
namespace {
  STATISTIC (NullChecks ,    "Poolchecks with NULL pool descriptor");
  STATISTIC (FullChecks ,    "Poolchecks with non-NULL pool descriptor");

  STATISTIC (PoolChecks ,    "Poolchecks Added");
  STATISTIC (FuncChecks ,    "Indirect Function Call Checks Added");
  STATISTIC (AlignLSChecks,  "Number of alignment checks on loads/stores");
  STATISTIC (MissedVarArgs , "Vararg functions not processed");
}

////////////////////////////////////////////////////////////////////////////
// InsertPoolChecks Methods
////////////////////////////////////////////////////////////////////////////

void
InsertPoolChecks::addCheckProto(Module &M) {
  intrinsic = &getAnalysis<InsertSCIntrinsic>();

  PoolCheck 		= intrinsic->getIntrinsic("sc.lscheck").F;
  PoolCheckUI 		= intrinsic->getIntrinsic("sc.lscheckui").F;
  PoolCheckAlign 	= intrinsic->getIntrinsic("sc.lscheckalign").F;
  PoolCheckAlignUI 	= intrinsic->getIntrinsic("sc.lscheckalignui").F;
  PoolCheckArray 	= intrinsic->getIntrinsic("sc.boundscheck").F;
  PoolCheckArrayUI 	= intrinsic->getIntrinsic("sc.boundscheckui").F;
#if 0
  ExactCheck		= intrinsic->getIntrinsic("sc.exactcheck").F;
#endif
  FunctionCheck 	= intrinsic->getIntrinsic("sc.funccheck").F;

  //
  // Mark poolcheck() as only reading memory.
  //
  PoolCheck->setOnlyReadsMemory();
  PoolCheckUI->setOnlyReadsMemory();
  PoolCheckAlign->setOnlyReadsMemory();
  PoolCheckAlignUI->setOnlyReadsMemory();

  // Special cases for var args
}

bool
InsertPoolChecks::runOnFunction(Function &F) {
  //
  // FIXME:
  //  This is incorrect; a function pass should *never* modify anything outside
  //  of the function on which it is given.  This should be done in the pass's
  //  doInitialization() method.
  //
  static bool uninitialized = true;
  if (uninitialized) {
    addCheckProto(*F.getParent());
    uninitialized = false;
  }

  TD       = &getAnalysis<TargetData>();
  abcPass  = &getAnalysis<ArrayBoundsCheckGroup>();
  poolPass = &getAnalysis<QueryPoolPass>();
  dsnPass  = &getAnalysis<DSNodePass>();
  paPass   = dsnPass->paPass;
  assert (paPass && "Pool Allocation Transform *must* be run first!");

  //
  // FIXME:
  //  We need to insert checks for variadic functions, too.
  //
  if (F.isVarArg())
    ++MissedVarArgs;
  else
    addPoolChecks(F);
  return true;
}

bool
InsertPoolChecks::doFinalization(Module &M) {
  //
	// Update the statistics.
  //
	PoolChecks = NullChecks + FullChecks;
	return true;
}

void
InsertPoolChecks::addPoolChecks(Function &F) {
  if (!DisableGEPChecks) {
    for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&*I))
        addGetElementPtrChecks (GEP);
    }
  }
  if (!DisableLSChecks)  addLoadStoreChecks(F);
}


//
// Method: insertAlignmentCheck()
//
// Description:
//  Insert an alignment check for the specified value.
//
void
InsertPoolChecks::insertAlignmentCheck (LoadInst * LI) {
  // Get the DSNode for the result of the load instruction.  If it is type
  // unknown, then no alignment check is needed.
  if (!(poolPass->isTypeKnown (LI)))
    return;

  //
  // Get the pool handle for the node.
  //
  Value *PH = ConstantPointerNull::get (getVoidPtrType());

  //
  // If the node is incomplete or unknown, then only perform the check if
  // checks to incomplete or unknown are allowed.
  //
  Constant * CheckAlignment = PoolCheckAlign;
  if ((poolPass->getDSFlags (LI)) & (DSNode::IncompleteNode | DSNode::UnknownNode)) {
#if 0
    if (EnableUnknownChecks) {
      CheckAlignment = PoolCheckAlignUI;
    } else {
      ++MissedIncompleteChecks;
      return;
    }
#else
    CheckAlignment = PoolCheckAlignUI;
    return;
#endif
  }

  //
  // A check is needed.  Fetch the alignment of the loaded pointer and insert
  // an alignment check.
  //
  Value * Alignment = poolPass->getAlignment (LI);
  assert (Alignment && "Cannot find alignment metadata!\n");

  // Insertion point for this check is *after* the load.
  BasicBlock::iterator InsertPt = LI;
  ++InsertPt;

  //
  // Create instructions to cast the checked pointer and the checked pool
  // into sbyte pointers.
  //
  Value *CastLI  = castTo (LI, getVoidPtrType(), InsertPt);
  Value *CastPHI = castTo (PH, getVoidPtrType(), InsertPt);

  // Create the call to poolcheckalign
  std::vector<Value *> args(1, CastPHI);
  args.push_back(CastLI);
  args.push_back (Alignment);
  CallInst::Create (CheckAlignment, args.begin(), args.end(), "", InsertPt);

  // Update the statistics
  ++AlignLSChecks;

  return;
}

//
// Method: addLSChecks()
//
// Description:
//  Add a load/store check or an indirect function call check for the specified
//  value.
//
// Inputs:
//  Vnew        - The pointer operand of the load/store instruction.
//  V           - ?
//  Instruction - The load, store, or call instruction requiring a check.
//  F           - The parent function of the instruction
//
// Notes:
//  FIXME: Indirect function call checks should be inserted by another method
//         (or more ideally, another pass).  This is especially true since
//         there are faster indirect function call check methods than the one
//         implemented here.
//
void
InsertPoolChecks::addLSChecks (Value *Vnew,
                               const Value *V,
                               Instruction *I,
                               Function *F) {
  //
  // This may be a load instruction that loads a pointer that:
  //  1) Points to a type known pool, and
  //  2) Loaded from a type unknown pool
  //
  // If this is the case, we need to perform an alignment check on the result
  // of the load.  Do that here.
  //
  if (LoadInst * LI = dyn_cast<LoadInst>(I)) {
    insertAlignmentCheck (LI);
  }

  Value * PH = ConstantPointerNull::get (getVoidPtrType());
  unsigned DSFlags = poolPass->getDSFlags (V);
  DSNode* Node = dsnPass->getDSNode(V, F);
  assert (Node && "No DSNode for checked pointer!\n");

  //
  // Do not perform checks on incomplete nodes.  While external heap
  // allocations can be recorded via hooking functionality in the system's
  // original allocator routines, external globals and stack allocations remain
  // invisible.
  //
  if (DSFlags & DSNode::IncompleteNode) return;
  if (DSFlags & DSNode::ExternalNode) return;

  //
  // Determine whether a load/store check (or an indirect call check) is
  // required on the pointer.  These checks are required in the following
  // circumstances:
  //
  //  1) All Type-Unknown pointers.  These can be pointing anywhere.
  //  2) Type-Known pointers into an array.  If we reach this point in the
  //     code, then no previous GEP check has verified that this pointer is
  //     within bounds.  Therefore, a load/store check is needed to ensure that
  //     the pointer is within bounds.
  //  3) Pointers that may have been integers casted into pointers.
  //
  if ((!(poolPass->isTypeKnown (V))) ||
      (DSFlags & (DSNode::ArrayNode | DSNode::IntToPtrNode))) {
    // I am amazed the check here since the commet says that I is an load/store
    // instruction! 
    if (dyn_cast<CallInst>(I)) {
      // Do not perform function checks on incomplete nodes
      if (DSFlags & DSNode::IncompleteNode) return;

      // Get the globals list corresponding to the node
      std::vector<const Function *> FuncList;
      Node->addFullFunctionList(FuncList);
      std::vector<const Function *>::iterator flI= FuncList.begin(), flE = FuncList.end();
      unsigned num = FuncList.size();
      if (flI != flE) {
        const Type* csiType = IntegerType::getInt32Ty(getGlobalContext());
        Value *NumArg = ConstantInt::get(csiType, num);	
               
        CastInst *CastVI = 
          CastInst::CreatePointerCast (Vnew, 
           getVoidPtrType(), "casted", I);

        std::vector<Value *> args(1, NumArg);
        args.push_back(CastVI);
        for (; flI != flE ; ++flI) {
          Function *func = (Function *)(*flI);
          CastInst *CastfuncI = 
            CastInst::CreatePointerCast (func, 
             getVoidPtrType(), "casted", I);
          args.push_back(CastfuncI);
        }
        CallInst::Create(FunctionCheck, args.begin(), args.end(), "", I);

        //
        // Update statistics on the number of indirect function call checks.
        //
        ++FuncChecks;
      }
    } else {
      //
      // FIXME:
      //  The code below should also perform the optimization for heap
      //  allocations (which appear as calls to an allocator function).
      //
      // FIXME:
      //  The next two lines should ensure that the allocation size is large
      //  enough for whatever value is being loaded/stored.
      //
      // If the pointer used for the load/store check is trivially seen to be
      // valid (load/store to allocated memory or a global variable), don't
      // bother doing a check.
      //
      if ((isa<AllocaInst>(Vnew)) || (isa<GlobalVariable>(Vnew)))
        return;

      CastInst *CastVI = 
        CastInst::CreatePointerCast (Vnew, 
               getVoidPtrType(), "casted", I);
      CastInst *CastPHI = 
        CastInst::CreatePointerCast (PH, 
               getVoidPtrType(), "casted", I);
      std::vector<Value *> args(1,CastPHI);
      args.push_back(CastVI);

      bool isUI = (DSFlags & (DSNode::IncompleteNode | DSNode::UnknownNode));
      Constant * PoolCheckFunc =  isUI ? PoolCheckUI : PoolCheck;
      CallInst::Create (PoolCheckFunc, args.begin(), args.end(), "", I);
    }
  }
}

void InsertPoolChecks::addLoadStoreChecks(Function &F){
  //here we check that we only do this on original functions
  //and not the cloned functions, the cloned functions may not have the
  //DSG

  bool isClonedFunc = false;
  Function *Forig = &F;
  PA::FuncInfo *FI = NULL;

  if (!SCConfig.svaEnabled()) {
    if (paPass->getFuncInfo(F))
      isClonedFunc = false;
    else
      isClonedFunc = true;
    
    FI = paPass->getFuncInfoOrClone(F);
    if (isClonedFunc) {
      Forig = paPass->getOrigFunctionFromClone(&F);
    }
  }

  //we got the original function

  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&*I)) {
      // We need to get the LI from the original function
      Value *P = LI->getPointerOperand();
      if (isClonedFunc) {
        assert (FI && "No FuncInfo for this function\n");
        assert((FI->MapValueToOriginal(LI)) && " not in the value map \n");
        const LoadInst *temp = dyn_cast<LoadInst>(FI->MapValueToOriginal(LI));
        assert(temp && " Instruction  not there in the NewToOldValue map");
        const Value *Ptr = temp->getPointerOperand();
        addLSChecks(P, Ptr, LI, Forig);
      } else {
        addLSChecks(P, P, LI, Forig);
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&*I)) {
      Value *P = SI->getPointerOperand();
      if (isClonedFunc) {
        SI->dump();
#if 0
        assert(FI->NewToOldValueMap.count(SI) && " not in the value map \n");
#else
        assert((FI->MapValueToOriginal(SI)) && " not in the value map \n");
#endif
#if 0
        const StoreInst *temp = dyn_cast<StoreInst>(FI->NewToOldValueMap[SI]);
#else
        const StoreInst *temp = dyn_cast<StoreInst>(FI->MapValueToOriginal(SI));
#endif
        assert(temp && " Instruction  not there in the NewToOldValue map");
        const Value *Ptr = temp->getPointerOperand();
        addLSChecks(P, Ptr, SI, Forig);
      } else {
        addLSChecks(P, P, SI, Forig);
      }
    } else if (CallInst *CI = dyn_cast<CallInst>(&*I)) {
      Value *FunctionOp = CI->getOperand(0);
      if (!isa<Function>(FunctionOp->stripPointerCasts())) {
        if (isClonedFunc) {
          assert(FI->MapValueToOriginal(CI) && " not in the value map \n");
          const CallInst *temp = dyn_cast<CallInst>(FI->MapValueToOriginal(CI));
          assert(temp && " Instruction  not there in the NewToOldValue map");
          const Value* FunctionOp1 = temp->getOperand(0);
          addLSChecks(FunctionOp, FunctionOp1, CI, Forig);
        } else {
          addLSChecks(FunctionOp, FunctionOp, CI, Forig);
        }
      }
    } 
  }
}

void
InsertPoolChecks::addGetElementPtrChecks (GetElementPtrInst * GEP) {
  if (abcPass->isGEPSafe(GEP))
    return;

  Instruction * iCurrent = GEP;

    // We have the GetElementPtr
    if (!isa<GetElementPtrInst>(*iCurrent)) {
      //Then this must be a function call
      //FIXME, get strcpy and others from the backup dir and adjust them for LLVA
      //Right now I just add memset &llva_memcpy for LLVA
      //      std::cerr << " function call \n";
      return;
    }
    // Now we need to decide if we need to pass in the alignmnet
    //for the poolcheck
    //     if (getDSNodeOffset(GEP->getPointerOperand(), F)) {
    //       std::cerr << " we don't handle middle of structs yet\n";
    //assert(!getDSNodeOffset(GEP->getPointerOperand(), F) && " we don't handle middle of structs yet\n");
    //       ++MissChecks;
    //       continue;
    //     }
    
    Instruction *Casted = GEP;
#if 0
    //
    // JTC: Disabled.  I'm not sure why we would look up a cloned value when
    //                 processing an old value.
    //
std::cerr << "Parent: " << GEP->getParent()->getParent()->getName() << std::endl;
std::cerr << "Ins   : " << *GEP << std::endl;
    if (!FI->ValueMap.empty()) {
      assert(FI->ValueMap.count(GEP) && "Instruction not in the value map \n");
      Instruction *temp = dyn_cast<Instruction>(FI->ValueMap[GEP]);
      assert(temp && " Instruction  not there in the Value map");
      Casted  = temp;
    }
#endif
    if (isa<GetElementPtrInst>(Casted)) {
      Value * PH = ConstantPointerNull::get (getVoidPtrType());
#if 0
      if (!PH) {
        Value *PointerOperand = GEPNew->getPointerOperand();
        if (ConstantExpr *cExpr = dyn_cast<ConstantExpr>(PointerOperand)) {
          if ((cExpr->getOpcode() == Instruction::Trunc) ||
              (cExpr->getOpcode() == Instruction::ZExt) ||
              (cExpr->getOpcode() == Instruction::SExt) ||
              (cExpr->getOpcode() == Instruction::FPToUI) ||
              (cExpr->getOpcode() == Instruction::FPToSI) ||
              (cExpr->getOpcode() == Instruction::UIToFP) ||
              (cExpr->getOpcode() == Instruction::SIToFP) ||
              (cExpr->getOpcode() == Instruction::FPTrunc) ||
              (cExpr->getOpcode() == Instruction::FPExt) ||
              (cExpr->getOpcode() == Instruction::PtrToInt) ||
              (cExpr->getOpcode() == Instruction::IntToPtr) ||
              (cExpr->getOpcode() == Instruction::BitCast))
            PointerOperand = cExpr->getOperand(0);
        }
        if (GlobalVariable *GV = dyn_cast<GlobalVariable>(PointerOperand)) {
          if (const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType())) {
            // We need to insert an actual check.  It could be a select
            // instruction.
            // First get the size.
            // This only works for one or two dimensional arrays.
            if (GEPNew->getNumOperands() == 2) {
              Value *secOp = GEPNew->getOperand(1);
              if (secOp->getType() != Type::Int32Ty) {
                secOp = CastInst::CreateSExtOrBitCast(secOp, Type::Int32Ty,
                                     secOp->getName()+".ec.casted", Casted);
              }

              const Type* csiType = Type::Int32Ty;
              std::vector<Value *> args(1,secOp);
              args.push_back(ConstantInt::get(csiType,AT->getNumElements()));
              CallInst::Create(ExactCheck,args.begin(), args.end(), "", Casted);
              DEBUG(std::cerr << "Inserted exact check call Instruction \n");
              return;
            } else if (GEPNew->getNumOperands() == 3) {
              if (ConstantInt *COP = dyn_cast<ConstantInt>(GEPNew->getOperand(1))) {
                // FIXME: assuming that the first array index is 0
                assert((COP->getZExtValue() == 0) && "non zero array index\n");
                Value * secOp = GEPNew->getOperand(2);
                if (secOp->getType() != Type::Int32Ty) {
                  secOp = CastInst::CreateSExtOrBitCast(secOp, Type::Int32Ty,
                                       secOp->getName()+".ec2.casted", Casted);
                }
                std::vector<Value *> args(1,secOp);
                const Type* csiType = Type::Int32Ty;
                args.push_back(ConstantInt::get(csiType,AT->getNumElements()));
                CallInst::Create(ExactCheck, args.begin(), args.end(), "", getNextInst(Casted));
                return;
              } else {
                // Handle non constant index two dimensional arrays later
                abort();
              }
            } else {
              // Handle Multi dimensional cases later
              DEBUG(std::cerr << "WARNING: Handle multi dimensional globals later\n");
              GEP->dump();
            }
          }
          DEBUG(std::cerr << " Global variable ok \n");
        }

        //      These must be real unknowns and they will be handled anyway
        //      std::cerr << " WARNING, DID NOT HANDLE   \n";
        //      (*iCurrent)->dump();
        return ;
      } else {
#endif
        {
        //
        // Determine if this is a pool belonging to a cloned version of the
        // function.  If so, do not add a pool check.
        //
        if (Instruction * InsPH = dyn_cast<Instruction>(PH)) {
          if ((InsPH->getParent()->getParent()) !=
              (Casted->getParent()->getParent()))
            return;
        }

        BasicBlock::iterator InsertPt = Casted;
        ++InsertPt;
        Casted            = castTo (Casted,
                                    getVoidPtrType(),
                                    (Casted)->getName()+".pc.casted",
                                    InsertPt);

        Value * CastedSrc = castTo (GEP->getPointerOperand(),
                                    getVoidPtrType(),
                                    (Casted)->getName()+".pcsrc.casted",
                                    InsertPt);

        Value *CastedPH = castTo (PH,
                                  getVoidPtrType(),
                                  "jtcph",
                                  InsertPt);
        std::vector<Value *> args(1, CastedPH);
        args.push_back(CastedSrc);
        args.push_back(Casted);

        // Insert it
        unsigned DSFlags = poolPass->getDSFlags (GEP);

        Instruction * CI;
        if ((!(poolPass->isTypeKnown (GEP))) || (DSFlags & DSNode::UnknownNode))
          CI = CallInst::Create(PoolCheckArrayUI, args.begin(), args.end(),
                                "", InsertPt);
        else
          CI = CallInst::Create(PoolCheckArray, args.begin(), args.end(),
                                "", InsertPt);

        DEBUG(std::cerr << "inserted instrcution \n");
      }
    }
}

NAMESPACE_SC_END
