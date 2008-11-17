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
#include "safecode/VectorListHelper.h"

#define REG_FUNC(var, ret, name, ...) do { var = M.getOrInsertFunction(name, FunctionType::get(ret, args<const Type*>::list(__VA_ARGS__), false)); } while (0);

char llvm::InsertPoolChecks::ID = 0;
char llvm::ClearCheckAttributes::ID = 0;

static llvm::RegisterPass<InsertPoolChecks> ipcPass ("safecode", "insert runtime checks");

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
  STATISTIC (NullChecks ,
                             "Poolchecks with NULL pool descriptor");
  STATISTIC (FullChecks ,
                             "Poolchecks with non-NULL pool descriptor");

  STATISTIC (PoolChecks , "Poolchecks Added");
  STATISTIC (AlignLSChecks,  "Number of alignment checks on loads/stores");
  STATISTIC (MissedVarArgs , "Vararg functions not processed");
#ifdef LLVA_KERNEL
  STATISTIC (MissChecks ,
                             "Poolchecks omitted due to bad pool descriptor");
  STATISTIC (BoundChecks,
                             "Bounds checks inserted");

  STATISTIC (MissedIncompleteChecks ,
                               "Poolchecks missed because of incompleteness");
  STATISTIC (MissedMultDimArrayChecks ,
                                           "Multi-dimensional array checks");

  STATISTIC (MissedStackChecks  , "Missed stack checks");
  STATISTIC (MissedGlobalChecks , "Missed global checks");
  STATISTIC (MissedNullChecks   , "Missed PD checks");
#endif
}

namespace llvm {
////////////////////////////////////////////////////////////////////////////
// InsertPoolChecks Methods
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
// Static Functions
////////////////////////////////////////////////////////////////////////////

//
// Function: getNextInst()
//
// Description:
//  Get the next instruction following this instruction.
//
// Return value:
//  0 - There is no instruction after this instruction in the Basic Block.
//  Otherwise, a pointer to the next instruction is returned.
//
static Instruction *
getNextInst (Instruction * Inst) {
  BasicBlock::iterator i(Inst);
  ++i;
  if ((i) == Inst->getParent()->getInstList().end())
    return 0;
  return i;
}

//
// Function: isEligableForExactCheck()
//
// Return value:
//  true  - This value is eligable for an exactcheck.
//  false - This value is not eligable for an exactcheck.
//
static inline bool
isEligableForExactCheck (Value * Pointer, bool IOOkay) {
  if ((isa<AllocationInst>(Pointer)) || (isa<GlobalVariable>(Pointer)))
    return true;

  if (CallInst* CI = dyn_cast<CallInst>(Pointer)) {
    if (CI->getCalledFunction()) {
#ifdef LLVA_KERNEL
      if ((CI->getCalledFunction()->getName() == "__vmalloc" ||
           CI->getCalledFunction()->getName() == "kmalloc" ||
           CI->getCalledFunction()->getName() == "kmem_cache_alloc" ||
           CI->getCalledFunction()->getName() == "__alloc_bootmem")) {
        return true;
      }

      if (IOOkay && (CI->getCalledFunction()->getName() == "__ioremap")) {
        return true;
      }
#else
      if (CI->getCalledFunction()->getName() == "poolalloc") {
        return true;
      }
#endif
      if (CI->getCalledFunction()->getName() == "malloc") {
        return true;
      }
    }
  }

  return false;
}

//
// Function: findSourcePointer()
//
// Description:
//  Given a pointer value, attempt to find a source of the pointer that can
//  be used in an exactcheck().
//
// Outputs:
//  indexed - Flags whether the data flow went through a indexing operation
//            (i.e. a GEP).  This value is always written.
//
static Value *
findSourcePointer (Value * PointerOperand, bool & indexed, bool IOOkay = true) {
  //
  // Attempt to look for the originally allocated object by scanning the data
  // flow up.
  //
  indexed = false;
  Value * SourcePointer = PointerOperand;
  Value * OldSourcePointer = 0;
  while (!isEligableForExactCheck (SourcePointer, IOOkay)) {
    assert (OldSourcePointer != SourcePointer);
    OldSourcePointer = SourcePointer;

    // Check for GEP and cast constant expressions
    if (ConstantExpr * cExpr = dyn_cast<ConstantExpr>(SourcePointer)) {
      if ((cExpr->isCast()) ||
          (cExpr->getOpcode() == Instruction::GetElementPtr)) {
        if (isa<PointerType>(cExpr->getOperand(0)->getType())) {
          SourcePointer = cExpr->getOperand(0);
          continue;
        }
      }
      // We cannot handle this expression; break out of the loop
      break;
    }

    // Check for GEP and cast instructions
    if (GetElementPtrInst * G = dyn_cast<GetElementPtrInst>(SourcePointer)) {
      SourcePointer = G->getPointerOperand();
      indexed = true;
      continue;
    }

    if (CastInst * CastI = dyn_cast<CastInst>(SourcePointer)) {
      if (isa<PointerType>(CastI->getOperand(0)->getType())) {
        SourcePointer = CastI->getOperand(0);
        continue;
      }
      break;
    }

    // Check for call instructions to exact checks.
    CallInst * CI1;
    if ((CI1 = dyn_cast<CallInst>(SourcePointer)) &&
        (CI1->getCalledFunction()) &&
        (CI1->getCalledFunction()->getName() == "exactcheck3")) {
      SourcePointer = CI1->getOperand (2);
      continue;
    }

    // We can't scan through any more instructions; give up
    break;
  }

  if (isEligableForExactCheck (SourcePointer, IOOkay))
    PointerOperand = SourcePointer;

  return PointerOperand;
}


//
// Function: addExactCheck2()
//
// Description:
//  Utility routine that inserts a call to exactcheck2().
//  It also records checked pointers in dsNodePass::checkedValues.
// 
// Inputs:
//  BasePointer   - An LLVM Value representing the base of the object to check.
//  Result        - An LLVM Value representing the pointer to check.
//  Bounds        - An LLVM Value representing the bounds of the check.
//  InsertPt      - The instruction before which to insert the check.
//
void
InsertPoolChecks::addExactCheck2 (Value * BasePointer,
                                  Value * Result,
                                  Value * Bounds,
                                  Instruction * InsertPt) {
  Value * ResultPointer = Result;

  // The LLVM type for a void *
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 

  //
  // Cast the operands to the correct type.
  //
  if (BasePointer->getType() != VoidPtrType)
    BasePointer = castTo (BasePointer, VoidPtrType,
                          BasePointer->getName()+".ec2.casted",
                          InsertPt);

  if (ResultPointer->getType() != VoidPtrType)
    ResultPointer = castTo (ResultPointer, VoidPtrType,
                            ResultPointer->getName()+".ec2.casted",
                            InsertPt);

  Value * CastBounds = Bounds;
  if (Bounds->getType() != Type::Int32Ty)
    CastBounds = castTo (Bounds, Type::Int32Ty,
                         Bounds->getName()+".ec.casted", InsertPt);

  //
  // Create the call to exactcheck2().
  //
  std::vector<Value *> args(1, BasePointer);
  args.push_back(ResultPointer);
  args.push_back(CastBounds);
  Instruction * CI;
  CI = CallInst::Create (ExactCheck2, args.begin(), args.end(), "", InsertPt);

  //
  // Record that this value was checked.
  //
  dsnPass->addCheckedValue(Result);

#if 0
  //
  // Replace the old pointer with the return value of exactcheck2(); this
  // prevents GCC from removing it completely.
  //
  if (CI->getType() != GEP->getType())
    CI = castTo (CI, GEP->getType(), GEP->getName(), InsertPt);

  Value::use_iterator UI = GEP->use_begin();
  for (; UI != GEP->use_end(); ++UI) {
    if (((*UI) != CI) && ((*UI) != ResultPointer))
      UI->replaceUsesOfWith (GEP, CI);
  }
#endif

  return;
}


//
// Function: insertExactCheck()
//
// Description:
//  Attepts to insert an efficient, accurate array bounds check for the given
//  GEP instruction; this check will not use Pools are MetaPools.
//
// Return value:
//  true  - An exactcheck() was successfully added.
//  false - An exactcheck() could not be added; a more extensive check will be
//          needed.
//
bool
InsertPoolChecks::insertExactCheck (GetElementPtrInst * GEP) {
  // The pointer operand of the GEP expression
  Value * PointerOperand = GEP->getPointerOperand();

  //
  // Get the DSNode for the instruction
  //
  Function *F   = GEP->getParent()->getParent();
  DSNode * Node = dsnPass->getDSNode(GEP, F);
  assert (Node && "boundscheck: DSNode is NULL!");

#if 0
  //
  // Determine whether an alignment check is needed.  This occurs when a DSNode
  // is type unknown (collapsed) but has pointers to type known (uncollapsed)
  // DSNodes.
  //
  if (preSCPass->nodeNeedsAlignment (Node)) {
    ++AlignChecks;
  }
#endif

  //
  // Attempt to find the object which we need to check.
  //
  bool WasIndexed = true;
  PointerOperand = findSourcePointer (PointerOperand, WasIndexed);

  //
  // Ensure the pointer operand really is a pointer.
  //
  if (!isa<PointerType>(PointerOperand->getType()))
    return false;

  //
  // Find the insertion point for the run-time check.
  //
  //BasicBlock::iterator InsertPt = AI->getParent()->begin();
  BasicBlock::iterator InsertPt = GEP;
  ++InsertPt;

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(PointerOperand)) {
    //
    // Attempt to remove checks on GEPs that only index into structures.
    // These criteria must be met:
    //  1) The pool must be Type-Homogoneous.
    //
#if 0
    if ((!(Node->isNodeCompletelyFolded())) &&
        (indexesStructsOnly (GEP))) {
      ++StructGEPsRemoved;
      return true;
    }
#endif

    //
    // Attempt to use a call to exactcheck() to check this value if it is a
    // global variable and, if it is a global array, it has a non-zero size.
    // We do not check zero length arrays because in C they are often used to
    // declare an external array of unknown size as follows:
    //        extern struct foo the_array[];
    //
    const Type * GlobalType = GV->getType()->getElementType();
    const ArrayType *AT = dyn_cast<ArrayType>(GlobalType);
    if ((!AT) || (AT->getNumElements())) {
      Value* Size = ConstantInt::get (Type::Int32Ty,
                                      TD->getABITypeSize(GlobalType));
      addExactCheck2 (PointerOperand, GEP, Size, InsertPt);
      return true;
    }
  }

  //
  // If the pointer was generated by a dominating alloca instruction, we can
  // do an exactcheck on it, too.
  //
  if (AllocationInst *AI = dyn_cast<AllocationInst>(PointerOperand)) {
    //
    // Attempt to remove checks on GEPs that only index into structures.
    // These criteria must be met:
    //  1) The pool must be Type-Homogoneous.
    //
#if 0
    if ((!(Node->isNodeCompletelyFolded())) &&
        (indexesStructsOnly (GEP))) {
      ++StructGEPsRemoved;
      return true;
    }
#endif

    const Type * AllocaType = AI->getAllocatedType();
    Value *AllocSize=ConstantInt::get(Type::Int32Ty, TD->getABITypeSize(AllocaType));

    if (AI->isArrayAllocation())
      AllocSize = BinaryOperator::Create(Instruction::Mul,
                                         AllocSize,
                                         AI->getOperand(0), "sizetmp", GEP);

    addExactCheck2 (PointerOperand, GEP, AllocSize, InsertPt);
    return true;
  }

  //
  // If the pointer was an allocation, we should be able to do exact checks
  //
  CallInst* CI = dyn_cast<CallInst>(PointerOperand);
  if (CI && (CI->getCalledFunction())) {
    if ((CI->getCalledFunction()->getName() == "__vmalloc") || 
        (CI->getCalledFunction()->getName() == "kmalloc") || 
        (CI->getCalledFunction()->getName() == "malloc") || 
        (CI->getCalledFunction()->getName() == "__alloc_bootmem")) {
      //
      // Attempt to remove checks on GEPs that only index into structures.
      // These criteria must be met:
      //  1) The pool must be Type-Homogoneous.
      //
#if 0
      if ((!(Node->isNodeCompletelyFolded())) &&
          (indexesStructsOnly (GEP))) {
        ++StructGEPsRemoved;
        return true;
      }
#endif

      Value* Cast = castTo (CI->getOperand(1), Type::Int32Ty, "", GEP);
      addExactCheck2(PointerOperand, GEP, Cast, InsertPt);
      return true;
    } else if (CI->getCalledFunction()->getName() == "__ioremap") {
      Value* Cast = castTo (CI->getOperand(2), Type::Int32Ty, "", GEP);
      addExactCheck2(PointerOperand, GEP, Cast, InsertPt);
      return true;
    } else if (CI->getCalledFunction()->getName() == "poolalloc") {
      Value* Cast = castTo (CI->getOperand(2), Type::Int32Ty, "", GEP);
      addExactCheck2(PointerOperand, GEP, Cast, InsertPt);
      return true;
    }
  }

  //
  // If the pointer is to a structure, we may be able to perform a simple
  // exactcheck on it, too, unless the array is at the end of the structure.
  // Then, we assume it's a variable length array and must be full checked.
  //
#if 0
  if (const PointerType * PT = dyn_cast<PointerType>(PointerOperand->getType()))
    if (const StructType *ST = dyn_cast<StructType>(PT->getElementType())) {
      const Type * CurrentType = ST;
      ConstantInt * C;
      for (unsigned index = 2; index < GEP->getNumOperands() - 1; ++index) {
        //
        // If this GEP operand is a constant, index down into the next type.
        //
        if (C = dyn_cast<ConstantInt>(GEP->getOperand(index))) {
          if (const StructType * ST2 = dyn_cast<StructType>(CurrentType)) {
            CurrentType = ST2->getElementType(C->getZExtValue());
            continue;
          }

          if (const ArrayType * AT = dyn_cast<ArrayType>(CurrentType)) {
            CurrentType = AT->getElementType();
            continue;
          }

          // We don't know how to handle this type of element
          break;
        }

        //
        // If the GEP operand is not constant and points to an array type,
        // then try to insert an exactcheck().
        //
        const ArrayType * AT;
        if ((AT = dyn_cast<ArrayType>(CurrentType)) && (AT->getNumElements())) {
          const Type* csiType = Type::getPrimitiveType(Type::Int32Ty);
          ConstantInt * Bounds = ConstantInt::get(csiType,AT->getNumElements());
          addExactCheck (GEP, GEP->getOperand (index), Bounds);
          return true;
        }
      }
    }
#endif

  /*
   * We were not able to insert a call to exactcheck().
   */
  return false;
}


void
InsertPoolChecks::addCheckProto(Module &M) {
  static const Type * VoidTy = Type::VoidTy;
  static const Type * Int32Ty = Type::Int32Ty;
  static const Type * vpTy = PointerType::getUnqual(Type::Int8Ty);

  REG_FUNC (PoolCheck,        VoidTy, "poolcheck",          vpTy, vpTy)
  REG_FUNC (PoolCheckUI,      VoidTy, "poolcheckui",        vpTy, vpTy)
  REG_FUNC (PoolCheckAlign,   VoidTy, "poolcheckalign",     vpTy, vpTy, Int32Ty)
  REG_FUNC (PoolCheckAlignUI, VoidTy, "poolcheckalignui",   vpTy, vpTy, Int32Ty)
  REG_FUNC (PoolCheckArray,   vpTy,   "boundscheck",        vpTy, vpTy, vpTy)
  REG_FUNC (PoolCheckArrayUI, vpTy,   "boundscheckui",      vpTy, vpTy, vpTy)
  REG_FUNC (ExactCheck,       VoidTy, "exactcheck",         Int32Ty, Int32Ty)
  REG_FUNC (ExactCheck2,      vpTy,   "exactcheck2",         vpTy, vpTy, Int32Ty)
  std::vector<const Type *> FArg3(1, Type::Int32Ty);
  FArg3.push_back(vpTy);
  FArg3.push_back(vpTy);
  FunctionType *FunctionCheckTy = FunctionType::get(Type::VoidTy, FArg3, true);
  FunctionCheck = M.getOrInsertFunction("funccheck", FunctionCheckTy);
  REG_FUNC (GetActualValue,   vpTy,   "pchk_getActualValue",vpTy, vpTy)

  //
  // Mark poolcheck() as only reading memory.
  //
  if (Function * F = dyn_cast<Function>(PoolCheck))
    F->setOnlyReadsMemory();
  if (Function * F = dyn_cast<Function>(PoolCheckUI))
    F->setOnlyReadsMemory();

  // Special cases for var args
}
  
bool
InsertPoolChecks::doInitialization(Module &M) {
  addCheckProto(M);
  return true;
}

bool
InsertPoolChecks::runOnFunction(Function &F) {
  abcPass = &getAnalysis<ArrayBoundsCheck>();
#ifndef LLVA_KERNEL
  paPass = &getAnalysis<PoolAllocateGroup>();
  // paPass = getAnalysisToUpdate<PoolAllocateGroup>();
  assert (paPass && "Pool Allocation Transform *must* be run first!");
  TD  = &getAnalysis<TargetData>();
#endif
  dsnPass = &getAnalysis<DSNodePass>();

  //std::cerr << "Running on Function " << F.getName() << std::endl;

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
    Function::iterator fI = F.begin(), fE = F.end();
    for ( ; fI != fE; ++fI) {
      BasicBlock * BB = fI;
      addGetElementPtrChecks (BB);
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
  // Get the function containing the load instruction
  Function * F = LI->getParent()->getParent();

  // Get the DSNode for the result of the load instruction.  If it is type
  // unknown, then no alignment check is needed.
  DSNode * LoadResultNode = dsnPass->getDSNode (LI,F);
  if (!(LoadResultNode && (!(LoadResultNode->isNodeCompletelyFolded())))) {
    return;
  }

  //
  // Get the pool handle for the node.
  //
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(LI, F, *FI);
  if (!PH) return;

  //
  // If the node is incomplete or unknown, then only perform the check if
  // checks to incomplete or unknown are allowed.
  //
  Constant * ThePoolCheckFunction = PoolCheckAlign;
  if ((LoadResultNode->isUnknownNode()) ||
      (LoadResultNode->isIncompleteNode())) {
#if 0
    if (EnableUnknownChecks) {
      ThePoolCheckFunction = PoolCheckAlignUI;
    } else {
      ++MissedIncompleteChecks;
      return;
    }
#else
    ThePoolCheckFunction = PoolCheckAlignUI;
    return;
#endif
  }

  //
  // A check is needed.  Scan through the links of the DSNode of the load's
  // pointer operand; we need to determine the offset for the alignment check.
  //
  DSNode * Node = dsnPass->getDSNode (LI->getPointerOperand(), F);
  if (!Node) return;
  for (unsigned i = 0 ; i < Node->getNumLinks(); i+=4) {
    DSNodeHandle & LinkNode = Node->getLink(i);
    if (LinkNode.getNode() == LoadResultNode) {
      // Insertion point for this check is *after* the load.
      BasicBlock::iterator InsertPt = LI;
      ++InsertPt;

      // Create instructions to cast the checked pointer and the checked pool
      // into sbyte pointers.
      Value *CastVI  = castTo (LI, PointerType::getUnqual(Type::Int8Ty), InsertPt);
      Value *CastPHI = castTo (PH, PointerType::getUnqual(Type::Int8Ty), InsertPt);

      // Create the call to poolcheck
      std::vector<Value *> args(1,CastPHI);
      args.push_back(CastVI);
      args.push_back (ConstantInt::get(Type::Int32Ty, LinkNode.getOffset()));
      CallInst::Create (ThePoolCheckFunction,args.begin(), args.end(), "", InsertPt);

      // Update the statistics
      ++AlignLSChecks;

      break;
    }
  }
}

#ifndef LLVA_KERNEL
//
// Method: addLSChecks()
//
// Inputs:
//  Vnew        - The pointer operand of the load/store instruction.
//  V           - ?
//  Instruction - The load or store instruction
//  F           - The parent function of the instruction
//
void
InsertPoolChecks::addLSChecks (Value *Vnew,
                               const Value *V,
                               Instruction *I,
                               Function *F) {
  //
  //
  // FIXME:
  //  This optimization is not safe.  We need to ensure that the memory is
  //  not freed between the previous check and this check.
  //
  // If we've already checked this pointer, don't bother checking it again.
  //
  if (dsnPass->isValueChecked (Vnew))
    return;

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

  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = dsnPass->getPoolHandle(V, F, *FI );
  DSNode* Node = dsnPass->getDSNode(V, F);
  if (!PH) {
    return;
  }

  if (isa<ConstantPointerNull>(PH)) {
    //we have a collapsed/Unknown pool
    Value *PH = dsnPass->getPoolHandle(V, F, *FI, true); 
#if 0
    assert (PH && "Null pool handle!\n");
#else
    if (!PH) return;
#endif
  }

  //
  // Do not perform checks on incomplete nodes.  While external heap
  // allocations can be recorded via hooking functionality in the system's
  // original allocator routines, external globals and stack allocations remain
  // invisible.
  //
  if (Node && (Node->isIncompleteNode())) return;
  if (Node && (Node->isExternalNode())) return;

  //
  // Only check pointers to type-unknown objects.
  //
  if (Node && Node->isNodeCompletelyFolded()) {
    if (dyn_cast<CallInst>(I)) {
      // Do not perform function checks on incomplete nodes
      if (Node->isIncompleteNode()) return;

      // Get the globals list corresponding to the node
      std::vector<const Function *> FuncList;
      Node->addFullFunctionList(FuncList);
      std::vector<const Function *>::iterator flI= FuncList.begin(), flE = FuncList.end();
      unsigned num = FuncList.size();
      if (flI != flE) {
        const Type* csiType = Type::Int32Ty;
        Value *NumArg = ConstantInt::get(csiType, num);	
               
        CastInst *CastVI = 
          CastInst::CreatePointerCast (Vnew, 
           PointerType::getUnqual(Type::Int8Ty), "casted", I);

        std::vector<Value *> args(1, NumArg);
        args.push_back(CastVI);
        for (; flI != flE ; ++flI) {
          Function *func = (Function *)(*flI);
          CastInst *CastfuncI = 
            CastInst::CreatePointerCast (func, 
             PointerType::getUnqual(Type::Int8Ty), "casted", I);
          args.push_back(CastfuncI);
        }
        CallInst::Create(FunctionCheck, args.begin(), args.end(), "", I);
      }
    } else {
      //
      // FIXME:
      //  The next two lines should ensure that the allocation size is large
      //  enough for whatever value is being loaded/stored.
      //
      // If the pointer used for the load/store check is trivially seen to be
      // valid (load/store to allocated memory or a global variable), don't
      // bother doing a check.
      //
      if ((isa<AllocationInst>(Vnew)) || (isa<GlobalVariable>(Vnew)))
        return;

      bool indexed = true;
      Value * SourcePointer = findSourcePointer (Vnew, indexed, false);
      if (isEligableForExactCheck (SourcePointer, false)) {
        Value * AllocSize;
        if (AllocationInst * AI = dyn_cast<AllocationInst>(SourcePointer)) {
          AllocSize = ConstantInt::get (Type::Int32Ty,
                                        TD->getABITypeSize(AI->getAllocatedType()));
          if (AI->isArrayAllocation()) {
            AllocSize = BinaryOperator::Create (Instruction::Mul,
                                               AllocSize,
                                               AI->getArraySize(), "sizetmp", I);
          }
        } else if (GlobalVariable * GV = dyn_cast<GlobalVariable>(SourcePointer)) {
          AllocSize = ConstantInt::get (Type::Int32Ty,
                                        TD->getABITypeSize(GV->getType()->getElementType()));
        } else if (CallInst * CI = dyn_cast<CallInst>(SourcePointer)) {
          assert (CI->getCalledFunction() && "Indirect call!\n");
          if (CI->getCalledFunction()->getName() == "poolalloc") {
            AllocSize = CI->getOperand(2);
          } else {
            assert (0 && "Cannot recognize allocator for source pointer!\n");
          }
        } else {
          assert (0 && "Cannot handle source pointer!\n");
        }

        addExactCheck2 (SourcePointer, Vnew, AllocSize, I);
      } else {
        CastInst *CastVI = 
          CastInst::CreatePointerCast (Vnew, 
                 PointerType::getUnqual(Type::Int8Ty), "casted", I);
        CastInst *CastPHI = 
          CastInst::CreatePointerCast (PH, 
                 PointerType::getUnqual(Type::Int8Ty), "casted", I);
        std::vector<Value *> args(1,CastPHI);
        args.push_back(CastVI);

        dsnPass->addCheckedDSNode(Node);
        dsnPass->addCheckedValue(Vnew);
        Constant * PoolCheckFunc = (Node->isIncompleteNode()) ? PoolCheckUI
                                                              : PoolCheck;
        CallInst::Create (PoolCheckFunc, args.begin(), args.end(), "", I);
      }
    }
  }
}

void InsertPoolChecks::addLoadStoreChecks(Function &F){
  //here we check that we only do this on original functions
  //and not the cloned functions, the cloned functions may not have the
  //DSG
  bool isClonedFunc = false;
  if (paPass->getFuncInfo(F))
    isClonedFunc = false;
  else
    isClonedFunc = true;
  Function *Forig = &F;
  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(F);
  if (isClonedFunc) {
      Forig = paPass->getOrigFunctionFromClone(&F);
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
        std::cerr << *(SI) << std::endl;
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
      if (!isa<Function>(FunctionOp)) {
        std::cerr << "JTC: Indirect Function Call Check: "
                  << F.getName() << " : " << *(CI->getOperand(0)) << std::endl;
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

#endif

void
InsertPoolChecks::addGetElementPtrChecks (BasicBlock * BB) {
  std::set<Instruction *> * UnsafeGetElemPtrs = abcPass->getUnsafeGEPs (BB);
  if (!UnsafeGetElemPtrs)
    return;

  std::set<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs->begin(),
                                          iEnd     = UnsafeGetElemPtrs->end();
  for (; iCurrent != iEnd; ++iCurrent) {
    if (dsnPass->isValueChecked(*iCurrent))
      continue;

    // We have the GetElementPtr
    if (!isa<GetElementPtrInst>(*iCurrent)) {
      //Then this must be a function call
      //FIXME, get strcpy and others from the backup dir and adjust them for LLVA
      //Right now I just add memset &llva_memcpy for LLVA
      //      std::cerr << " function call \n";
#ifdef LLVA_KERNEL
      CallInst *CI = dyn_cast<CallInst>(*iCurrent);
      if (CI && (!DisableIntrinsicChecks)) {
        Value *Fop = CI->getOperand(0);
        Function *F = CI->getParent()->getParent();
        if (Fop->getName() == "llva_memcpy") {
          Value *PH = dsnPass->getPoolHandle(CI->getOperand(1), F); 
          Instruction *InsertPt = CI;
          if (!PH) {
            ++NullChecks;
            ++MissedNullChecks;

            // Don't bother to insert the NULL check unless the user asked
            if (!EnableNullChecks)
              continue;
            PH = Constant::getNullValue(PointerType::getUnqual(Type::Int8Ty));
          }
          CastInst *CastCIUint = 
            CastInst::CreatePointerCast(CI->getOperand(1), Type::Int32Ty, "node.lscasted", InsertPt);
          CastInst *CastCIOp3 = 
            CastInst::CreateZExtOrBitCast(CI->getOperand(3), Type::Int32Ty, "node.lscasted", InsertPt);
          Instruction *Bop = BinaryOperator::Create(Instruction::Add, CastCIUint,
                          CastCIOp3, "memcpyadd",InsertPt);
          
          // Create instructions to cast the checked pointer and the checked pool
          // into sbyte pointers.
          CastInst *CastSourcePointer = 
            CastInst::CreatePointerCast(CI->getOperand(1), 
                         PointerType::getUnqual(Type::Int8Ty), "memcpy.1.casted", InsertPt);
          CastInst *CastCI = 
            CastInst::CreatePointerCast(Bop, 
                         PointerType::getUnqual(Type::Int8Ty), "mempcy.2.casted", InsertPt);
          CastInst *CastPHI = 
            CastInst::CreatePointerCast(PH, 
                         PointerType::getUnqual(Type::Int8Ty), "poolhandle.lscasted", InsertPt);
          
          // Create the call to poolcheck
          std::vector<Value *> args(1,CastPHI);
          args.push_back(CastSourcePointer);
          args.push_back(CastCI);
          CallInst::Create(PoolCheckArray,args.begin(), args.end(),"", InsertPt);
#if 0
        } else if (Fop->getName() == "memset") {
          Value *PH = getPoolHandle(CI->getOperand(1), F); 
          Instruction *InsertPt = CI->getNext();
          if (!PH) {
            NullChecks++;
            // Don't bother to insert the NULL check unless the user asked
            if (!EnableNullChecks)
              continue;
            PH = Constant::getNullValue(PointerType::getUnqual(Type::Int8Ty));
          }
          CastInst *CastCIUint = 
            CastInst::CreatePointerCast(CI, Type::Int32Ty, "node.lscasted", InsertPt);
          CastInst *CastCIOp3 = 
            CastInst::CreateZExtOrBitCast(CI->getOperand(3), Type::Int32Ty, "node.lscasted", InsertPt);
          Instruction *Bop = BinaryOperator::Create(Instruction::Add, CastCIUint,
                          CastCIOp3, "memsetadd",InsertPt);
          
          // Create instructions to cast the checked pointer and the checked pool
          // into sbyte pointers.
          CastInst *CastSourcePointer = 
            CastInst::CreatePointerCast(CI->getOperand(1), 
                         PointerType::getUnqual(Type::Int8Ty), "memset.1.casted", InsertPt);
          CastInst *CastCI = 
            CastInst::CreatePointerCast(Bop, 
                         PointerType::getUnqual(Type::Int8Ty), "memset.2.casted", InsertPt);
          CastInst *CastPHI = 
            CastInst::CreatePointerCast(PH, 
                         PointerType::getUnqual(Type::Int8Ty), "poolhandle.lscasted", InsertPt);
          
          // Create the call to poolcheck
          std::vector<Value *> args(1,CastPHI);
          args.push_back(CastSourcePointer);
          args.push_back(CastCI);
          CallInst::Create(PoolCheckArray,args,"", InsertPt);
#endif
        }
      }
#endif
      continue;
    }
    GetElementPtrInst *GEP = cast<GetElementPtrInst>(*iCurrent);
    Function *F = GEP->getParent()->getParent();
    // Now we need to decide if we need to pass in the alignmnet
    //for the poolcheck
    //     if (getDSNodeOffset(GEP->getPointerOperand(), F)) {
    //       std::cerr << " we don't handle middle of structs yet\n";
    //assert(!getDSNodeOffset(GEP->getPointerOperand(), F) && " we don't handle middle of structs yet\n");
    //       ++MissChecks;
    //       continue;
    //     }
    
#ifndef LLVA_KERNEL    
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
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
    if (GetElementPtrInst *GEPNew = dyn_cast<GetElementPtrInst>(Casted)) {
      Value *PH = dsnPass->getPoolHandle(GEP, F, *FI);
      if (PH && isa<ConstantPointerNull>(PH)) continue;
      if (insertExactCheck (GEPNew)) {
        DSNode * Node = dsnPass->getDSNode (GEP, F);
        dsnPass->addCheckedDSNode(Node);
        // checked value is inserted by addExactCheck2(), which is called by insertExactCheck()
        continue;
      }

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
              continue;
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
                continue;
              } else {
                // Handle non constant index two dimensional arrays later
                abort();
              }
            } else {
              // Handle Multi dimensional cases later
              DEBUG(std::cerr << "WARNING: Handle multi dimensional globals later\n");
              (*iCurrent)->dump();
            }
          }
          DEBUG(std::cerr << " Global variable ok \n");
        }

        //      These must be real unknowns and they will be handled anyway
        //      std::cerr << " WARNING, DID NOT HANDLE   \n";
        //      (*iCurrent)->dump();
        continue ;
      } else {
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
                                    PointerType::getUnqual(Type::Int8Ty),
                                    (Casted)->getName()+".pc.casted",
                                    InsertPt);

        Value * CastedSrc = castTo (GEP->getPointerOperand(),
                                    PointerType::getUnqual(Type::Int8Ty),
                                    (Casted)->getName()+".pcsrc.casted",
                                    InsertPt);

        Value *CastedPH = castTo (PH,
                                  PointerType::getUnqual(Type::Int8Ty),
                                  "jtcph",
                                  InsertPt);
        std::vector<Value *> args(1, CastedPH);
        args.push_back(CastedSrc);
        args.push_back(Casted);

        // Insert it
        DSNode * Node = dsnPass->getDSNode (GEP, F);
        dsnPass->addCheckedDSNode(Node);
        dsnPass->addCheckedValue(GEPNew);

        Instruction * CI;
        if (Node->isIncompleteNode())
          CI = CallInst::Create(PoolCheckArrayUI, args.begin(), args.end(),
                                "", InsertPt);
        else
          CI = CallInst::Create(PoolCheckArray, args.begin(), args.end(),
                                "", InsertPt);

        //
        // Replace the uses of the original pointer with the result of the
        // boundscheck.
        //
#if SC_ENABLE_OOB
        CI = castTo (CI, GEP->getType(), GEP->getName(), InsertPt);

        Value::use_iterator UI = GEP->use_begin();
        for (; UI != GEP->use_end(); ++UI) {
          if (((*UI) != CI) && ((*UI) != Casted))
            UI->replaceUsesOfWith (GEP, CI);
        }
#endif

        DEBUG(std::cerr << "inserted instrcution \n");
      }
    }
#else
    //
    // Get the pool handle associated with the pointer operand.
    //
    Value *PH = dsnPass->getPoolHandle(GEP->getPointerOperand(), F);
    GetElementPtrInst *GEPNew = GEP;
    Instruction *Casted = GEP;

    DSGraph * TDG = TDPass->getDSGraph(*F);
    DSNode * Node = TDG->getNodeForValue(GEP).getNode();

    DEBUG(std::cerr << "LLVA: addGEPChecks: Pool " << PH << " Node ");
    DEBUG(std::cerr << Node << std::endl);

    Value *PointerOperand = GEPNew->getPointerOperand();
    if (ConstantExpr *cExpr = dyn_cast<ConstantExpr>(PointerOperand)) {
      if (cExpr->getOpcode() == Instruction::Cast)
        PointerOperand = cExpr->getOperand(0);
    }
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(PointerOperand)) {
      if (const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType())) {
        // we need to insert an actual check
        // It could be a select instruction
        // First get the size
        // This only works for one or two dimensional arrays
        if (GEPNew->getNumOperands() == 2) {
          Value *secOp = GEPNew->getOperand(1);
          if (secOp->getType() != Type::Int32Ty) {
            secOp = CastInst::CreateSExtOrBitCast(secOp, Type::Int32Ty,
                                 secOp->getName()+".ec3.casted", Casted);
          }
          
          std::vector<Value *> args(1,secOp);
          const Type* csiType = Type::getPrimitiveType(Type::Int32TyID);
          args.push_back(ConstantInt::get(csiType,AT->getNumElements()));
          CallInst *newCI = CallInst::Create(ExactCheck,args,"", Casted);
          ++BoundChecks;
          //	    DEBUG(std::cerr << "Inserted exact check call Instruction \n");
          continue;
        } else if (GEPNew->getNumOperands() == 3) {
          if (ConstantInt *COP = dyn_cast<ConstantInt>(GEPNew->getOperand(1))) {
            //FIXME assuming that the first array index is 0
            assert((COP->getZExtValue() == 0) && "non zero array index\n");
            Value * secOp = GEPNew->getOperand(2);
            if (secOp->getType() != Type::Int32Ty) {
              secOp = CastInst::CreateSExtOrBitCast(secOp, Type::Int32Ty,
                                   secOp->getName()+".ec4.casted", Casted);
            }
            std::vector<Value *> args(1,secOp);
            const Type* csiType = Type::getPrimitiveType(Type::Int32TyID);
            args.push_back(ConstantInt::get(csiType,AT->getNumElements()));
            CallInst *newCI = CallInst::Create(ExactCheck,args,"", Casted->getNext());
            ++BoundChecks;
            continue;
          } else {
            //Handle non constant index two dimensional arrays later
            abort();
          }
        } else {
          //Handle Multi dimensional cases later
          std::cerr << "WARNING: Handle multi dimensional globals later\n";
          (*iCurrent)->dump();
          ++MissedMultDimArrayChecks;
        }
        DEBUG(std::cerr << " Global variable ok \n");
      }
    }

#if 0
    //No checks for incomplete nodes 
    if (!EnableIncompleteChecks) {
      if (Node->isIncomplete()) {
        ++MissedNullChecks;
        continue;
      }
    }
#endif

    //
    // We cannot insert an exactcheck().  Insert a pool check.
    //
    if (!PH) {
      DEBUG(std::cerr << "missing GEP check: Null PH: " << GEP << "\n");
      ++NullChecks;
      if (!PH) ++MissedNullChecks;

      // Don't bother to insert the NULL check unless the user asked
      if (!EnableNullChecks)
      {
        continue;
      }
      PH = Constant::getNullValue(PointerType::getUnqual(Type::Int8Ty));
    } else {
      //
      // Determine whether the pool handle dominates the pool check.
      // If not, then don't insert it.
      //

      //
      // FIXME:
      //  This domination check is too restrictive; it eliminates pools that do
      //  dominate but are outside of the current basic block.
      //
      // Only add the pool check if the pool is a global value or it belongs
      // to the same basic block.
      //
      if (isa<GlobalValue>(PH)) {
        ++FullChecks;
      } else if (isa<Instruction>(PH)) {
        Instruction * IPH = (Instruction *)(PH);
        if (IPH->getParent() == Casted->getParent()) {
          //
          // If the instructions belong to the same basic block, ensure that
          // the pool dominates the load/store.
          //
          Instruction * IP = IPH;
          for (IP=IPH; (IP->isTerminator()) || (IP==Casted); IP=IP->getNext()) {
            ;
          }
          if (IP == Casted)
            ++FullChecks;
          else {
            ++MissChecks;
            continue;
          }
        } else {
          ++MissChecks;
          continue;
        }
      } else {
        ++MissChecks;
        continue;
      }
    }

    //
    // Regardless of the node type, always perform an accurate bounds check.
    //
    Instruction *InsertPt = Casted->getNext();
    if (Casted->getType() != PointerType::getUnqual(Type::Int8Ty)) {
      Casted = CastInst::CreatePointerCast(Casted,PointerType::getUnqual(Type::Int8Ty),
                            (Casted)->getName()+".pc2.casted",InsertPt);
    }
    Instruction *CastedPointerOperand = CastInst::CreatePointerCast(PointerOperand,
                                         PointerType::getUnqual(Type::Int8Ty),
                                         PointerOperand->getName()+".casted",InsertPt);
    Instruction *CastedPH = CastInst::CreatePointerCast(PH,
                                         PointerType::getUnqual(Type::Int8Ty),
                                         "ph",InsertPt);
    std::vector<Value *> args(1, CastedPH);
    args.push_back(CastedPointerOperand);
    args.push_back(Casted);
    CallInst * newCI = CallInst::Create(PoolCheckArray, args, "",InsertPt);
#endif    
  }
}

#undef REG_FUNC

/// CODES for LLVA_KERNEL

#ifdef LLVA_KERNEL
//
// Method: addLSChecks()
//
// Description:
//  Insert a poolcheck() into the code for a load or store instruction.
//
void InsertPoolChecks::addLSChecks(Value *V, Instruction *I, Function *F) {
  DSGraph * TDG = TDPass->getDSGraph(*F);
  DSNode * Node = TDG->getNodeForValue(V).getNode();
  
  if (Node && Node->isNodeCompletelyFolded()) {
    if (!EnableIncompleteChecks) {
      if (Node->isIncomplete()) {
        ++MissedIncompleteChecks;
        return;
      }
    }
    // Get the pool handle associated with this pointer.  If there is no pool
    // handle, use a NULL pointer value and let the runtime deal with it.
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    Value *PH = dsnPass->getPoolHandle(V, F, *FI);
#ifdef DEBUG
std::cerr << "LLVA: addLSChecks: Pool " << PH << " Node " << Node << std::endl;
#endif
    // FIXME: We cannot handle checks to global or stack positions right now.
    if ((!PH) || (Node->isAllocaNode()) || (Node->isGlobalNode())) {
      ++NullChecks;
      if (!PH) ++MissedNullChecks;
      if (Node->isAllocaNode()) ++MissedStackChecks;
      if (Node->isGlobalNode()) ++MissedGlobalChecks;

      // Don't bother to insert the NULL check unless the user asked
      if (!EnableNullChecks)
        return;
      PH = Constant::getNullValue(PointerType::getUnqual(Type::Int8Ty));
    } else {
      //
      // Only add the pool check if the pool is a global value or it
      // belongs to the same basic block.
      //
      if (isa<GlobalValue>(PH)) {
        ++FullChecks;
      } else if (isa<Instruction>(PH)) {
        Instruction * IPH = (Instruction *)(PH);
        if (IPH->getParent() == I->getParent()) {
          //
          // If the instructions belong to the same basic block, ensure that
          // the pool dominates the load/store.
          //
          Instruction * IP = IPH;
          for (IP=IPH; (IP->isTerminator()) || (IP == I); IP=IP->getNext()) {
            ;
          }
          if (IP == I)
            ++FullChecks;
          else {
            ++MissChecks;
            return;
          }
        } else {
          ++MissChecks;
          return;
        }
      } else {
        ++MissChecks;
        return;
      }
    }      
    // Create instructions to cast the checked pointer and the checked pool
    // into sbyte pointers.
    CastInst *CastVI = 
      CastInst::CreatePointerCast(V, 
		   PointerType::getUnqual(Type::Int8Ty), "node.lscasted", I);
    CastInst *CastPHI = 
      CastInst::CreatePointerCast(PH, 
		   PointerType::getUnqual(Type::Int8Ty), "poolhandle.lscasted", I);

    // Create the call to poolcheck
    std::vector<Value *> args(1,CastPHI);
    args.push_back(CastVI);
    CallInst::Create(PoolCheck,args,"", I);
  }
}

void
InsertPoolChecks::addLoadStoreChecks(Function &F) {
    for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&*I)) {
      Value *P = LI->getPointerOperand();
      addLSChecks(P, LI, F);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&*I)) {
      Value *P = SI->getPointerOperand();
      addLSChecks(P, SI, F);
    } else if (ICmpInst *CmpI = dyn_cast<ICmpInst>(&*I)) {
#ifdef SC_ENABLE_OOB
      switch (CmpI->getPredicate()) {
        ICmpInst::Predicate::ICMP_EQ:
        ICmpInst::Predicate::ICMP_NE:
          // Replace all pointer operands with the getActualValue() call
          assert ((CmpI->getNumOperands() == 2) &&
                   "nmber of operands for CmpI different from 2 ");
          if (isa<PointerType>(CmpI->getOperand(0)->getType())) {
            // we need to insert a call to getactualvalue
            // First get the poolhandle for the pointer
            // TODO: We don't have a working getactualvalue(), so don't waste
            // time calling it.
            if ((!isa<ConstantPointerNull>(CmpI->getOperand(0))) &&
                (!isa<ConstantPointerNull>(CmpI->getOperand(1)))) {
              addGetActualValue(CmpI, 0);
              addGetActualValue(CmpI, 1);
            }
          }
          break;

        default:
          break;
      }
#endif // endif for SC_ENABLE_OOB
    }
  }
}

void
InsertPoolChecks::addGetActualValue (ICmpInst *SCI, unsigned operand) {
#if 1
  // We know that the operand is a pointer type 
  Value *op   = SCI->getOperand(operand);
  Function *F = SCI->getParent()->getParent();

#ifndef LLVA_KERNEL    
#if 0
  // Some times the ECGraphs doesnt contain F for newly created cloned
  // functions
  if (!equivPass->ContainsDSGraphFor(*F)) {
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    op = FI->MapValueToOriginal(op);
    if (!op) return; //abort();
  }
#endif
#endif    

  Function *Fnew = F;
  Value *PH = 0;
  if (Argument *arg = dyn_cast<Argument>(op)) {
    Fnew = arg->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*Fnew);
    PH = dsnPass->getPoolHandle(op, Fnew, *FI);
  } else if (Instruction *Inst = dyn_cast<Instruction>(op)) {
    Fnew = Inst->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*Fnew);
    PH = dsnPass->getPoolHandle(op, Fnew, *FI);
  } else if (isa<Constant>(op)) {
    return;
    //      abort();
  } else if (!isa<ConstantPointerNull>(op)) {
    //has to be a global
    abort();
  }
  op = SCI->getOperand(operand);
  if (!isa<ConstantPointerNull>(op)) {
    if (PH) {
      if (1) { //HACK fixed
        const Type * VoidPtrType = PointerType::getUnqual(Type::Int8Ty);
        Value * PHVptr = castTo (PH, VoidPtrType, PH->getName()+".casted", SCI);
        Value * OpVptr = castTo (op, VoidPtrType, op->getName()+".casted", SCI);

        std::vector<Value *> args = make_vector(PHVptr, OpVptr,0);
        CallInst *CI = CallInst::Create (GetActualValue, args.begin(), args.end(), "getval", SCI);
        Instruction *CastBack = castTo (CI, op->getType(),
                                         op->getName()+".castback", SCI);
        SCI->setOperand (operand, CastBack);
      }
    } else {
      //It shouldn't work if PH is not null
    }
  }
#endif
}
#endif

}
