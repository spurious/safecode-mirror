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
#include "safecode/InsertChecks.h"
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

//
// Method: getDSNodeHandle()
//
// Description:
//  This method looks up the DSNodeHandle for a given LLVM value.  The context
//  of the value is the specified function, although if it is a global value,
//  the DSNodeHandle may exist within the global DSGraph.
//
// Return value:
//  A DSNodeHandle for the value is returned.  This DSNodeHandle could either
//  be in the function's DSGraph or from the GlobalsGraph.  Note that the
//  DSNodeHandle may represent a NULL DSNode.
//
DSNodeHandle
InsertPoolChecks::getDSNodeHandle (const Value * V, const Function * F) {
  //
  // Ensure that the function has a DSGraph
  //
  assert (dsaPass->hasDSGraph(*F) && "No DSGraph for function!\n");

  //
  // Lookup the DSNode for the value in the function's DSGraph.
  //
  DSGraph * TDG = dsaPass->getDSGraph(*F);
  DSNodeHandle DSH = TDG->getNodeForValue(V);

  //
  // If the value wasn't found in the function's DSGraph, then maybe we can
  // find the value in the globals graph.
  //
  if ((DSH.isNull()) && (isa<GlobalValue>(V))) {
    //
    // Try looking up this DSNode value in the globals graph.  Note that
    // globals are put into equivalence classes; we may need to first find the
    // equivalence class to which our global belongs, find the global that
    // represents all globals in that equivalence class, and then look up the
    // DSNode Handle for *that* global.
    //
    DSGraph * GlobalsGraph = TDG->getGlobalsGraph ();
    DSH = GlobalsGraph->getNodeForValue(V);
    if (DSH.isNull()) {
      //
      // DSA does not currently handle global aliases.
      //
      if (!isa<GlobalAlias>(V)) {
        //
        // We have to dig into the globalEC of the DSGraph to find the DSNode.
        //
        const GlobalValue * GV = dyn_cast<GlobalValue>(V);
        const GlobalValue * Leader;
        Leader = GlobalsGraph->getGlobalECs().getLeaderValue(GV);
        DSH = GlobalsGraph->getNodeForValue(Leader);
      }
    }
  }

  return DSH;
}

//
// Method: getDSNode()
//
// Description:
//  This method looks up the DSNode for a given LLVM value.  The context of the
//  value is the specified function, although if it is a global value, the
//  DSNode may exist within the global DSGraph.
//
// Return value:
//  NULL - No DSNode was found.
//  Otherwise, a pointer to the DSNode for this value is returned.  This DSNode
//  could either be in the function's DSGraph or from the GlobalsGraph.
//
DSNode *
InsertPoolChecks::getDSNode (const Value * V, const Function * F) {
  //
  // Simply return the DSNode referenced by the DSNodeHandle.
  //
  return getDSNodeHandle (V, F).getNode();
}

//
// Method: isTypeKnown()
//
// Description:
//  Determines whether the value is always used in a type-consistent fashion
//  within the program.
//
// Inputs:
//  V - The value to check for type-consistency.  This value *must* have a
//      DSNode.
//
// Return value:
//  true  - This value is always used in a type-consistent fashion.
//  false - This value is not guaranteed to be used in a type-consistent
//          fashion.
//
bool
InsertPoolChecks::isTypeKnown (const Value * V, const Function * F) {
  //
  // First, get the DSNode for the value.
  //
  DSNode * DSN = getDSNode (V, F);
  assert (DSN && "isTypeKnown(): No DSNode for the specified value!\n");

  //
  // Now determine if it is type-consistent.
  //
  return (!(DSN->isNodeCompletelyFolded()));
}

//
// Method: getDSFlags()
//
// Description:
//  Return the DSNode flags associated with the specified value.
//
// Inputs:
//  V - The value for which the DSNode flags are requested.  This value *must*
//      have a DSNode.
//
// Return Value:
//  The DSNode flags (which are a vector of bools in an unsigned int).
//
unsigned
InsertPoolChecks::getDSFlags (const Value * V, const Function * F) {
  //
  // First, get the DSNode for the value.
  //
  DSNode * DSN = getDSNode (V, F);
  assert (DSN && "getDSFlags(): No DSNode for the specified value!\n");

  //
  // Now return the flags for it.
  //
  return DSN->getNodeFlags();
}

//
// Method: getAlignment()
//
// Description:
//  Determine the offset into the object to which the specified value points.
//
unsigned
InsertPoolChecks::getOffset (const Value * V, const Function * F) {
  //
  // Get the DSNodeHandle for this pointer.
  //
  DSNodeHandle DSH = getDSNodeHandle (V, F);
  assert (!(DSH.isNull()));

  //
  // Return the offset into the object at which the pointer points.
  //
  return DSH.getOffset();
}

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

  TD      = &getAnalysis<TargetData>();
  abcPass = &getAnalysis<ArrayBoundsCheckGroup>();
  dsaPass = &getAnalysis<EQTDDataStructures>();

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
  //
  // Don't do alignment checks on non-pointer values.
  //
  if (!(isa<PointerType>(LI->getType())))
    return;

  //
  // Get the function in which the load instruction lives.
  //
  Function * F = LI->getParent()->getParent();

  //
  // Get the DSNode for the result of the load instruction.  If it is type
  // unknown, then no alignment check is needed.
  //
  if (!isTypeKnown (LI, F))
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
  if ((getDSFlags (LI, F)) & (DSNode::IncompleteNode | DSNode::UnknownNode)) {
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
  const Type * Int32Type = Type::getInt32Ty(F->getParent()->getContext());
  Value * Alignment = ConstantInt::get(Int32Type, getOffset (LI, F));

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

  //
  // Alignment checks are currently disabled because we're doing load/store
  // checks on all pointers.
  //
#if 0
  if (LoadInst * LI = dyn_cast<LoadInst>(I)) {
    insertAlignmentCheck (LI);
  }
#endif

  Value * PH = ConstantPointerNull::get (getVoidPtrType());
  unsigned DSFlags = getDSFlags (V, F);
  DSNode* Node = getDSNode (V, F);
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
  // FIXME:
  //  The type-known optimization is only applicable when dangling pointer
  //  errors are dealt with correctly (such as using garbage collection or
  //  automatic pool allocation) or when the points-to analysis is modified to
  //  reflect type inconsistencies that can occur through dangling pointer
  //  dereferences.  Since none of these options is currently working when
  //  pool allocation is performed after check insertion, we have to turn this
  //  optimization off.
  //
#if 0
  if ((!(isTypeKnown (V, F))) ||
      (DSFlags & (DSNode::ArrayNode | DSNode::IntToPtrNode))) {
#else
  if (1) {
#endif
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

//
// Method: addLoadStoreChecks()
//
// Description:
//  Scan through all the instructions in the specified function and insert
//  run-time checks for load, store, and indirect call instructions.
//
void
InsertPoolChecks::addLoadStoreChecks (Function & F) {
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&*I)) {
      Value *P = LI->getPointerOperand();
      addLSChecks(P, P, LI, &F);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&*I)) {
      Value *P = SI->getPointerOperand();
      addLSChecks(P, P, SI, &F);
    } else if (CallInst *CI = dyn_cast<CallInst>(&*I)) {
      Value *FunctionOp = CI->getOperand(0);
      if (!isa<Function>(FunctionOp->stripPointerCasts())) {
        addLSChecks(FunctionOp, FunctionOp, CI, &F);
      }
    } 
  }
}

void
InsertPoolChecks::addGetElementPtrChecks (GetElementPtrInst * GEP) {
  if (abcPass->isGEPSafe(GEP))
    return;

  //
  // Get the function in which the GEP instruction lives.
  //
  Function * F = GEP->getParent()->getParent();

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
        unsigned DSFlags = getDSFlags (GEP, F);

        Instruction * CI;
        if ((!(isTypeKnown (GEP, F))) || (DSFlags & DSNode::UnknownNode))
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
