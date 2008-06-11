#include "safecode/Config/config.h"
#include "SCUtils.h"
#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include <iostream>
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

char llvm::InsertPoolChecks::ID = 0;

extern Value *getRepresentativeMetaPD(Value *);
RegisterPass<InsertPoolChecks> ipc("safecode", "insert runtime checks");

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
  STATISTIC (MissChecks ,
                             "Poolchecks omitted due to bad pool descriptor");
  STATISTIC (PoolChecks , "Poolchecks Added");
  STATISTIC (BoundChecks,
                             "Bounds checks inserted");

  STATISTIC (MissedIncompleteChecks ,
                               "Poolchecks missed because of incompleteness");
  STATISTIC (MissedMultDimArrayChecks ,
                                           "Multi-dimensional array checks");

  STATISTIC (MissedStackChecks  , "Missed stack checks");
  STATISTIC (MissedGlobalChecks , "Missed global checks");
  STATISTIC (MissedNullChecks   , "Missed PD checks");

  // Object registration statistics
  STATISTIC (StackRegisters,      "Stack registrations");
  STATISTIC (SavedRegAllocs,      "Stack registrations avoided");
}

////////////////////////////////////////////////////////////////////////////
// Static Functions
////////////////////////////////////////////////////////////////////////////

//
// Function: isEligableForExactCheck()
//
// Return value:
//  true  - This value is eligable for an exactcheck.
//  false - This value is not eligable for an exactcheck.
//
static inline bool
isEligableForExactCheck (Value * Pointer, bool IOOkay) {
  if ((isa<AllocaInst>(Pointer)) ||
      (isa<MallocInst>(Pointer)) ||
      (isa<GlobalVariable>(Pointer)))
    return true;

  if (CallInst* CI = dyn_cast<CallInst>(Pointer)) {
    if (CI->getCalledFunction() &&
        (CI->getCalledFunction()->getName() == "__vmalloc" || 
         CI->getCalledFunction()->getName() == "malloc" || 
         CI->getCalledFunction()->getName() == "kmalloc" || 
         CI->getCalledFunction()->getName() == "kmem_cache_alloc" || 
         CI->getCalledFunction()->getName() == "__alloc_bootmem")) {
      return true;
    }

    if (IOOkay && (CI->getCalledFunction()->getName() == "__ioremap")) {
      return true;
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

////////////////////////////////////////////////////////////////////////////
// InsertPoolChecks Methods
////////////////////////////////////////////////////////////////////////////

namespace llvm {
//
// Function: addExactCheck()
//
// Description:
//  Utility routine that inserts a call to exactcheck().  This function can
//  perform some optimization be determining if the arguments are constant.
//  If they are, we can forego inserting the call.
//
// Inputs:
//  Index - An LLVM Value representing the index of the access.
//  Bounds - An LLVM Value representing the bounds of the check.
//
void
InsertPoolChecks::addExactCheck (Value * Pointer,
                                 Value * Index, Value * Bounds,
                                 Instruction * InsertPt) {
#if 0
  //
  // Record that this value was checked.
  //
  CheckedValues.insert (Pointer);
#endif

  //
  // Attempt to determine statically if this check will always pass; if so,
  // then don't bother doing it at run-time.
  //
  ConstantInt * CIndex  = dyn_cast<ConstantInt>(Index);
  ConstantInt * CBounds = dyn_cast<ConstantInt>(Bounds);
  if (CIndex && CBounds) {
    int index  = CIndex->getSExtValue();
    int bounds = CBounds->getSExtValue();
    assert ((index >= 0) && "exactcheck: const negative index");
    assert ((index < bounds) && "exactcheck: const out of range");

    return;
  }

  //
  // Second, cast the operands to the correct type.
  //
  Value * CastIndex = Index;
  if (Index->getType() != Type::Int32Ty)
    CastIndex = castTo (Index, Type::Int32Ty,
                             Index->getName()+".ec.casted", InsertPt);

  Value * CastBounds = Bounds;
  if (Bounds->getType() != Type::Int32Ty)
    CastBounds = castTo (Bounds, Type::Int32Ty,
                              Bounds->getName()+".ec.casted", InsertPt);

  const Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
  Value * CastResult = Pointer;
  if (CastResult->getType() != VoidPtrType)
    CastResult = castTo (CastResult, VoidPtrType,
                              CastResult->getName()+".ec.casted", InsertPt);

  std::vector<Value *> args(1, CastIndex);
  args.push_back(CastBounds);
  args.push_back(CastResult);
  Instruction * CI = CallInst::Create (ExactCheck, args.begin(), args.end(), "ec", InsertPt);

#if 0
  //
  // Replace the old index with the return value of exactcheck(); this
  // prevents GCC from removing it completely.
  //
  Value * CastCI = CI;
  if (CI->getType() != GEP->getType())
    CastCI = castTo  (CI, GEP->getType(), GEP->getName(), InsertPt);

  Value::use_iterator UI = GEP->use_begin();
  for (; UI != GEP->use_end(); ++UI) {
    if (((*UI) != CI) && ((*UI) != CastResult))
      UI->replaceUsesOfWith (GEP, CastCI);
  }
#endif

  return;
}

//
// Function: addExactCheck2()
//
// Description:
//  Utility routine that inserts a call to exactcheck2().
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
#if 0
  CheckedValues.insert (Result);
#endif

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
  // The GEP instruction casted to the correct type
  Instruction *Casted = GEP;

  // The pointer operand of the GEP expression
  Value * PointerOperand = GEP->getPointerOperand();

  //
  // Get the DSNode for the instruction
  //
  Function *F   = GEP->getParent()->getParent();
  DSGraph & TDG = getDSGraph(*F);
  DSNode * Node = TDG.getNodeForValue(GEP).getNode();
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
    // global array with a non-zero size.  We do not check zero length arrays
    // because in C they are often used to declare an external array of unknown
    // size as follows:
    //        extern struct foo the_array[];
    //
    const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType());
    if ((!WasIndexed) && AT && (AT->getNumElements())) {
      Value* Size=ConstantInt::get(Type::Int32Ty, TD->getABITypeSize(AT));
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
      AllocSize = BinaryOperator::create(Instruction::Mul,
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

//
// Function: insertExactCheck()
//
// Description:
//  Attepts to insert an efficient, accurate array bounds check for the given
//  GEP instruction; this check will not use Pools are MetaPools.
//
// Inputs:
//  I        - The instruction for which we are adding the check.
//  Src      - The pointer that needs to be checked.
//  Size     - The size, in bytes, that will be read/written by instruction I.
//  InsertPt - The instruction before which the check should be inserted.
//
// Return value:
//  true  - An exactcheck() was successfully added.
//  false - An exactcheck() could not be added; a more extensive check will be
//          needed.
//
bool
InsertPoolChecks::insertExactCheck (Instruction * I,
                                    Value * Src,
                                    Value * Size,
                                    Instruction * InsertPt) {
  // The pointer operand of the GEP expression
  Value * PointerOperand = Src;

  //
  // Get the DSNode for the instruction
  //
#if 1
  Function *F   = I->getParent()->getParent();
  DSGraph & TDG = getDSGraph(*F);
  DSNode * Node = TDG.getNodeForValue(I).getNode();
  if (!Node)
    return false;
#endif

  //
  // Determine whether an alignment check is needed.  This occurs when a DSNode
  // is type unknown (collapsed) but has pointers to type known (uncollapsed)
  // DSNodes.
  //
#if 0
  if (preSCPass->nodeNeedsAlignment (Node)) {
    ++AlignChecks;
  }
#endif

  //
  // Attempt to find the original object for which this check applies.
  // This involves unpeeling casts, GEPs, etc.
  //
  bool WasIndexed = true;
  PointerOperand = findSourcePointer (PointerOperand, WasIndexed);

  //
  // Ensure the pointer operand really is a pointer.
  //
  if (!isa<PointerType>(PointerOperand->getType()))
  {
    return false;
  }

  //
  // Attempt to use a call to exactcheck() to check this value if it is a
  // global array with a non-zero size.  We do not check zero length arrays
  // because in C they are often used to declare an external array of unknown
  // size as follows:
  //        extern struct foo the_array[];
  //
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(PointerOperand)) {
    const Type* csiType = Type::Int32Ty;
    unsigned int arraysize = TD->getABITypeSize(GV->getType()->getElementType());
    ConstantInt * Bounds = ConstantInt::get(csiType, arraysize);
    if (WasIndexed)
      addExactCheck2 (PointerOperand, Src, Bounds, InsertPt);
    else
      addExactCheck (Src, Size, Bounds, InsertPt);
    return true;
  }

  //
  // If the pointer was generated by a dominating alloca instruction, we can
  // do an exactcheck on it, too.
  //
  if (AllocaInst *AI = dyn_cast<AllocaInst>(PointerOperand)) {
    const Type * AllocaType = AI->getAllocatedType();
    Value *AllocSize=ConstantInt::get(Type::Int32Ty, TD->getABITypeSize(AllocaType));

    if (AI->isArrayAllocation())
      AllocSize = BinaryOperator::create(Instruction::Mul,
                                         AllocSize,
                                         AI->getOperand(0), "allocsize", InsertPt);

    if (WasIndexed)
      addExactCheck2 (PointerOperand, Src, AllocSize, InsertPt);
    else
      addExactCheck (Src, Size, AllocSize, InsertPt);
    return true;
  }

  //
  // If the pointer was an allocation, we should be able to do exact checks
  //
  if(CallInst* CI = dyn_cast<CallInst>(PointerOperand)) {
    if (CI->getCalledFunction() && (
                              CI->getCalledFunction()->getName() == "__vmalloc" || 
                              CI->getCalledFunction()->getName() == "malloc" || 
                              CI->getCalledFunction()->getName() == "kmalloc")) {
      Value* Cast = castTo (CI->getOperand(1), Type::Int32Ty, "allocsize", InsertPt);
      if (WasIndexed)
        addExactCheck2 (PointerOperand, Src, Cast, InsertPt);
      else
        addExactCheck (Src, Size, Cast, InsertPt);
      return true;
    }
  }

  //
  // We were not able to insert a call to exactcheck().
  //
  return false;
}

bool InsertPoolChecks::runOnModule(Module &M) {
  abcPass = getAnalysisToUpdate<ArrayBoundsCheck>();
  //  budsPass = &getAnalysis<CompleteBUDataStructures>();
#ifndef LLVA_KERNEL  
  paPass = getAnalysisToUpdate<PoolAllocateGroup>();
#if 0
  if (!paPass)
    paPass = getAnalysisToUpdate<PoolAllocateSimple>();
#endif
  assert (paPass && "Pool Allocation Transform *must* be run first!");
  efPass = &getAnalysis<EmbeCFreeRemoval>();
  TD  = &getAnalysis<TargetData>();
#else
  TDPass = &getAnalysis<TDDataStructures>();
#endif

  //add the new poolcheck prototype 
  addPoolCheckProto(M);
#ifndef LLVA_KERNEL  
  //register global arrays and collapsed nodes with global pools
  registerGlobalArraysWithGlobalPools(M);
#endif

  // Replace old poolcheck with the new one 
  addPoolChecks(M);

  // Add stack registrations
  registerStackObjects (M);

  //
  // Update the statistics.
  //
  PoolChecks = NullChecks + FullChecks;
  
  return true;
}

#ifndef LLVA_KERNEL
void
InsertPoolChecks::registerGlobalArraysWithGlobalPools(Module &M) {
  //
  // Find the main() function.  For FORTRAN programs converted to C using the
  // NAG f2c tool, the function is named MAIN__.
  //
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    MainFunc = M.getFunction("MAIN__");
    if (MainFunc == 0 || MainFunc->isDeclaration()) {
      std::cerr << "Cannot do array bounds check for this program"
          << "no 'main' function yet!\n";
      abort();
    }
  }

  // First register, argc and argv
  Function::arg_iterator AI = MainFunc->arg_begin(), AE = MainFunc->arg_end();
  if (AI != AE) {
    //There is argc and argv
    Value *Argc = AI;
    AI++;
    Value *Argv = AI;
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*MainFunc);
    Value *PH= getPoolHandle(Argv, MainFunc, *FI);
    Constant *PoolRegister = paPass->PoolRegister;
    BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();

    // Insert the registration after all calls to poolinit().  Also skip
    // cast, alloca, and binary operators.
    while ((isa<CallInst>(InsertPt))  ||
            isa<CastInst>(InsertPt)   ||
            isa<AllocaInst>(InsertPt) ||
            isa<BinaryOperator>(InsertPt)) {
      if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
        if (Function * F = CI->getCalledFunction())
          if (F->getName() == "poolinit")
            ++InsertPt;
          else
            break;
        else
          break;
      else
        ++InsertPt;
    }

    if (PH) {
      Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
      Instruction *GVCasted = CastInst::createPointerCast(Argv,
					   VoidPtrType, Argv->getName()+"casted",InsertPt);
      const Type* csiType = Type::Int32Ty;
      Value *AllocSize = CastInst::createZExtOrBitCast(Argc,
				      csiType, Argc->getName()+"casted",InsertPt);
      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 ConstantInt::get(csiType, 4), "sizetmp", InsertPt);
      std::vector<Value *> args;
      args.push_back (PH);
      args.push_back (GVCasted);
      args.push_back (AllocSize);
      CallInst::Create(PoolRegister, args.begin(), args.end(), "", InsertPt); 
    } else {
      std::cerr << "argv's pool descriptor is not present. \n";
      //	abort();
    }
  }

  // Now iterate over globals and register all the arrays
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
  Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);

  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GI)) {
      // Don't register the llvm.used variable
      if (GV->getName() == "llvm.used") continue;
      if (GV->getType() != PoolDescPtrTy) {
        DSGraph &G = paPass->getGlobalsGraph();
        DSNode *DSN  = G.getNodeForValue(GV).getNode();
        Value * AllocSize;
        const Type* csiType = Type::Int32Ty;
        const Type * GlobalType = GV->getType()->getElementType();
        AllocSize = ConstantInt::get (csiType, TD->getABITypeSize(GlobalType));
        Constant *PoolRegister = paPass->PoolRegister;
        BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
        //skip the calls to poolinit
        while ((isa<CallInst>(InsertPt))  ||
                isa<CastInst>(InsertPt)   ||
                isa<AllocaInst>(InsertPt) ||
                isa<BinaryOperator>(InsertPt)) {
          if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
            if (Function * F = CI->getCalledFunction())
              if (F->getName() == "poolinit")
                ++InsertPt;
              else
                break;
            else
              break;
          else
            ++InsertPt;
        }

        Value * PH = paPass->getGlobalPool (DSN);
        if (PH) {
          Instruction *GVCasted = CastInst::createPointerCast(GV,
                 VoidPtrType, GV->getName()+"casted",InsertPt);
          std::vector<Value *> args;
          args.push_back (PH);
          args.push_back (GVCasted);
          args.push_back (AllocSize);
          CallInst::Create(PoolRegister, args.begin(), args.end(), "", InsertPt); 
        } else {
          std::cerr << "pool descriptor not present for " << *GV << std::endl;
    #if 0
          abort();
    #endif
        }
      }
    }
  }

  //
  // Initialize the runtime.
  //
  BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();

  // Insert the registration after all calls to poolinit().  Also skip
  // cast, alloca, and binary operators.
  while ((isa<CallInst>(InsertPt))  ||
          isa<CastInst>(InsertPt)   ||
          isa<AllocaInst>(InsertPt) ||
          isa<BinaryOperator>(InsertPt)) {
    if (CallInst * CI = dyn_cast<CallInst>(InsertPt))
      if (Function * F = CI->getCalledFunction())
        if (F->getName() == "poolinit")
          ++InsertPt;
        else
          break;
      else
        break;
    else
      ++InsertPt;
  }

  std::vector<Value *> args;
  args.push_back (ConstantInt::get(Type::Int32Ty,DanglingChecks,0));
  CallInst::Create(RuntimeInit, args.begin(), args.end(), "", InsertPt); 
}
#endif

void
InsertPoolChecks::registerStackObjects (Module &M) {
  for (Module::iterator FI = M.begin(); FI != M.end(); ++FI)
    for (Function::iterator BI = FI->begin(); BI != FI->end(); ++BI)
      for (BasicBlock::iterator I = BI->begin(); I != BI->end(); ++I)
        if (AllocaInst * AI = dyn_cast<AllocaInst>(I)) {
          registerAllocaInst (AI, AI);
        }
}

void
InsertPoolChecks::registerAllocaInst(AllocaInst *AI, AllocaInst *AIOrig) {
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
  DSNode *Node = getDSNode(AIOrig, FOrig);
  if (!Node) return;
  assert ((Node->isAllocaNode()) && "DSNode for alloca is missing stack flag!");

  //
  // Only register the stack allocation if it may be the subject of a run-time
  // check.  This can only occur when the object is used like an array because:
  //  1) GEP checks are only done when accessing arrays.
  //  2) Load/Store checks are only done on collapsed nodes (which appear to be
  //     used like arrays).
  //
#if 0
  if (!(Node->isArray()))
    return;
#endif

  //
  // Determine if any use (direct or indirect) escapes this function.  If not,
  // then none of the checks will consult the MetaPool, and we can forego
  // registering the alloca.
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
      if (CI1 = dyn_cast<CallInst>(UI)) {
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
  Value *PH = getPoolHandle(AIOrig, FOrig, *FI);
  if (PH == 0 || isa<ConstantPointerNull>(PH)) return;

  Value *AllocSize =
    ConstantInt::get(Type::Int32Ty, TD->getABITypeSize(AI->getAllocatedType()));
  
  if (AI->isArrayAllocation())
    AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
                                       AI->getOperand(0), "sizetmp", AI);

  // Insert object registration at the end of allocas.
  Instruction *iptI = AI;
  ++iptI;
  if (AI->getParent() == (&(AI->getParent()->getParent()->getEntryBlock()))) {
    BasicBlock::iterator InsertPt = AI->getParent()->begin();
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
#if 1
  CastedPH     = castTo (PH,
                         PointerType::getUnqual(Type::Int8Ty),
                         "allocph",Casted);
  args.clear();
  args.push_back (CastedPH);
  args.push_back (Casted);
  for (Function::iterator BB = AI->getParent()->getParent()->begin();
                          BB != AI->getParent()->getParent()->end();
                          ++BB) {
    iptI = BB->getTerminator();
    if (isa<ReturnInst>(iptI) || isa<UnwindInst>(iptI))
      CallInst::Create (StackFree, args.begin(), args.end(), "", iptI);
  }
#endif

  // Update statistics
  ++StackRegisters;
}

void InsertPoolChecks::addPoolChecks(Module &M) {
  if (!DisableGEPChecks) {
    Module::iterator mI = M.begin(), mE = M.end();
    for ( ; mI != mE; ++mI) {
      Function * F = mI;
      Function::iterator fI = F->begin(), fE = F->end();
      for ( ; fI != fE; ++fI) {
        BasicBlock * BB = fI;
        addGetElementPtrChecks (BB);
      }
    }
  }
  if (!DisableLSChecks)  addLoadStoreChecks(M);
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
    PH = getPoolHandle(op, Fnew, *FI);
  } else if (Instruction *Inst = dyn_cast<Instruction>(op)) {
    Fnew = Inst->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*Fnew);
    PH = getPoolHandle(op, Fnew, *FI);
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


#ifdef LLVA_KERNEL
//
// Method: addLSChecks()
//
// Description:
//  Insert a poolcheck() into the code for a load or store instruction.
//
void InsertPoolChecks::addLSChecks(Value *V, Instruction *I, Function *F) {
  DSGraph & TDG = TDPass->getDSGraph(*F);
  DSNode * Node = TDG.getNodeForValue(V).getNode();
  
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
    Value *PH = getPoolHandle(V, F, *FI);
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
      CastInst::createPointerCast(V, 
		   PointerType::getUnqual(Type::Int8Ty), "node.lscasted", I);
    CastInst *CastPHI = 
      CastInst::createPointerCast(PH, 
		   PointerType::getUnqual(Type::Int8Ty), "poolhandle.lscasted", I);

    // Create the call to poolcheck
    std::vector<Value *> args(1,CastPHI);
    args.push_back(CastVI);
    CallInst::Create(PoolCheck,args,"", I);
  }
}

void InsertPoolChecks::addLoadStoreChecks(Module &M){
  Module::iterator mI = M.begin(), mE = M.end();
  for ( ; mI != mE; ++mI) {
    Function *F = mI;
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (LoadInst *LI = dyn_cast<LoadInst>(&*I)) {
        Value *P = LI->getPointerOperand();
        addLSChecks(P, LI, F);
      } else if (StoreInst *SI = dyn_cast<StoreInst>(&*I)) {
        Value *P = SI->getPointerOperand();
        addLSChecks(P, SI, F);
      } else if (ICmpInst *CmpI = dyn_cast<ICmpInst>(&*I)) {
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
      }
    }
  }
}
#else
//
// Method: addLSChecks()
//
// Inputs:
//  Vnew        - The pointer operand of the load/store instruction.
//  V           - ?
//  Instruction - The load or store instruction
//  F           - The parent function of the instruction
//
void InsertPoolChecks::addLSChecks(Value *Vnew, const Value *V, Instruction *I, Function *F) {

  PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
  Value *PH = getPoolHandle(V, F, *FI );
  DSNode* Node = getDSNode(V, F);
  if (!PH) {
    return;
  }

  if (isa<ConstantPointerNull>(PH)) {
    //we have a collapsed/Unknown pool
    Value *PH = getPoolHandle(V, F, *FI, true); 
    assert (PH && "Null pool handle!\n");
  }

  //
  // Do not perform checks on incomplete nodes.  While external heap
  // allocations can be recorded via hooking functionality in the system's
  // original allocator routines, external globals and stack allocations remain
  // invisible.
  //
  if (Node && (Node->isIncompleteNode())) return;

  if (Node && Node->isNodeCompletelyFolded()) {
    if (dyn_cast<CallInst>(I)) {
      // Do not perform function checks on incomplete nodes
      if (Node->isIncompleteNode()) return;

      // Get the globals list corresponding to the node
      std::vector<Function *> FuncList;
      Node->addFullFunctionList(FuncList);
      std::vector<Function *>::iterator flI= FuncList.begin(), flE = FuncList.end();
      unsigned num = FuncList.size();
      if (flI != flE) {
        const Type* csiType = Type::Int32Ty;
        Value *NumArg = ConstantInt::get(csiType, num);	
               
        CastInst *CastVI = 
          CastInst::createPointerCast (Vnew, 
           PointerType::getUnqual(Type::Int8Ty), "casted", I);

        std::vector<Value *> args(1, NumArg);
        args.push_back(CastVI);
        for (; flI != flE ; ++flI) {
          Function *func = *flI;
          CastInst *CastfuncI = 
            CastInst::createPointerCast (func, 
             PointerType::getUnqual(Type::Int8Ty), "casted", I);
          args.push_back(CastfuncI);
        }
        CallInst::Create(FunctionCheck, args.begin(), args.end(), "", I);
      }
    } else {
      CastInst *CastVI = 
        CastInst::createPointerCast (Vnew, 
               PointerType::getUnqual(Type::Int8Ty), "casted", I);
      CastInst *CastPHI = 
        CastInst::createPointerCast (PH, 
               PointerType::getUnqual(Type::Int8Ty), "casted", I);
      std::vector<Value *> args(1,CastPHI);
      args.push_back(CastVI);

      if (Node->isIncompleteNode())
        CallInst::Create(PoolCheckUI,args.begin(), args.end(), "", I);
      else
        CallInst::Create(PoolCheck,args.begin(), args.end(), "", I);
    }
  }
}

void InsertPoolChecks::addLoadStoreChecks(Module &M){
  Module::iterator mI = M.begin(), mE = M.end();
  for ( ; mI != mE; ++mI) {
    Function *F = mI;
    //here we check that we only do this on original functions
    //and not the cloned functions, the cloned functions may not have the
    //DSG
    bool isClonedFunc = false;
    if (paPass->getFuncInfo(*F))
      isClonedFunc = false;
    else
      isClonedFunc = true;
    Function *Forig = F;
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    if (isClonedFunc) {
      Forig = paPass->getOrigFunctionFromClone(F);
    }
    //we got the original function

    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
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
          std::cerr << "JTC: LIC: " << F->getName() << " : " << *(CI->getOperand(0)) << std::endl;
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
          Value *PH = getPoolHandle(CI->getOperand(1), F); 
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
            CastInst::createPointerCast(CI->getOperand(1), Type::Int32Ty, "node.lscasted", InsertPt);
          CastInst *CastCIOp3 = 
            CastInst::createZExtOrBitCast(CI->getOperand(3), Type::Int32Ty, "node.lscasted", InsertPt);
          Instruction *Bop = BinaryOperator::create(Instruction::Add, CastCIUint,
                          CastCIOp3, "memcpyadd",InsertPt);
          
          // Create instructions to cast the checked pointer and the checked pool
          // into sbyte pointers.
          CastInst *CastSourcePointer = 
            CastInst::createPointerCast(CI->getOperand(1), 
                         PointerType::getUnqual(Type::Int8Ty), "memcpy.1.casted", InsertPt);
          CastInst *CastCI = 
            CastInst::createPointerCast(Bop, 
                         PointerType::getUnqual(Type::Int8Ty), "mempcy.2.casted", InsertPt);
          CastInst *CastPHI = 
            CastInst::createPointerCast(PH, 
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
            CastInst::createPointerCast(CI, Type::Int32Ty, "node.lscasted", InsertPt);
          CastInst *CastCIOp3 = 
            CastInst::createZExtOrBitCast(CI->getOperand(3), Type::Int32Ty, "node.lscasted", InsertPt);
          Instruction *Bop = BinaryOperator::create(Instruction::Add, CastCIUint,
                          CastCIOp3, "memsetadd",InsertPt);
          
          // Create instructions to cast the checked pointer and the checked pool
          // into sbyte pointers.
          CastInst *CastSourcePointer = 
            CastInst::createPointerCast(CI->getOperand(1), 
                         PointerType::getUnqual(Type::Int8Ty), "memset.1.casted", InsertPt);
          CastInst *CastCI = 
            CastInst::createPointerCast(Bop, 
                         PointerType::getUnqual(Type::Int8Ty), "memset.2.casted", InsertPt);
          CastInst *CastPHI = 
            CastInst::createPointerCast(PH, 
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
      Value *PH = getPoolHandle(GEP, F, *FI);
      if (PH && isa<ConstantPointerNull>(PH)) continue;
      if (insertExactCheck (GEPNew)) continue;
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
                secOp = CastInst::createSExtOrBitCast(secOp, Type::Int32Ty,
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
                  secOp = CastInst::createSExtOrBitCast(secOp, Type::Int32Ty,
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
        DSNode * Node = getDSNode (GEP, F);
        if (Node->isIncompleteNode())
          CallInst::Create(PoolCheckArrayUI, args.begin(), args.end(),
                           "", InsertPt);
        else
          CallInst::Create(PoolCheckArray, args.begin(), args.end(),
                           "", InsertPt);
        DEBUG(std::cerr << "inserted instrcution \n");
      }
    }
#else
    //
    // Get the pool handle associated with the pointer operand.
    //
    Value *PH = getPoolHandle(GEP->getPointerOperand(), F);
    GetElementPtrInst *GEPNew = GEP;
    Instruction *Casted = GEP;

    DSGraph & TDG = TDPass->getDSGraph(*F);
    DSNode * Node = TDG.getNodeForValue(GEP).getNode();

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
            secOp = CastInst::createSExtOrBitCast(secOp, Type::Int32Ty,
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
              secOp = CastInst::createSExtOrBitCast(secOp, Type::Int32Ty,
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
      Casted = CastInst::createPointerCast(Casted,PointerType::getUnqual(Type::Int8Ty),
                            (Casted)->getName()+".pc2.casted",InsertPt);
    }
    Instruction *CastedPointerOperand = CastInst::createPointerCast(PointerOperand,
                                         PointerType::getUnqual(Type::Int8Ty),
                                         PointerOperand->getName()+".casted",InsertPt);
    Instruction *CastedPH = CastInst::createPointerCast(PH,
                                         PointerType::getUnqual(Type::Int8Ty),
                                         "ph",InsertPt);
    std::vector<Value *> args(1, CastedPH);
    args.push_back(CastedPointerOperand);
    args.push_back(Casted);
    CallInst * newCI = CallInst::Create(PoolCheckArray,args, "",InsertPt);
#endif    
  }
}

void InsertPoolChecks::addPoolCheckProto(Module &M) {
  const Type * VoidPtrType = PointerType::getUnqual(Type::Int8Ty);
  /*
  const Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  //	StructType::get(make_vector<const Type*>(VoidPtrType, VoidPtrType,
  //                                               Type::Int32Ty, Type::Int32Ty, 0));
  const Type * PoolDescTypePtr = PointerType::getUnqual(PoolDescType);
  */  

  RuntimeInit = M.getOrInsertFunction("pool_init_runtime", Type::VoidTy,
                                                           Type::Int32Ty, 0);

  std::vector<const Type *> Arg(1, VoidPtrType);
  Arg.push_back(VoidPtrType);
  FunctionType *PoolCheckTy =
    FunctionType::get(Type::VoidTy,Arg, false);
  PoolCheck   = M.getOrInsertFunction("poolcheck", PoolCheckTy);
  PoolCheckUI = M.getOrInsertFunction("poolcheckui", PoolCheckTy);

  std::vector<const Type *> Arg2(1, VoidPtrType);
  Arg2.push_back(VoidPtrType);
  Arg2.push_back(VoidPtrType);
  FunctionType *PoolCheckArrayTy =
    FunctionType::get(Type::VoidTy,Arg2, false);
  PoolCheckArray   = M.getOrInsertFunction("boundscheck",   PoolCheckArrayTy);
  PoolCheckArrayUI = M.getOrInsertFunction("boundscheckui", PoolCheckArrayTy);
  
  std::vector<const Type *> FArg2(1, Type::Int32Ty);
  FArg2.push_back(Type::Int32Ty);
  FunctionType *ExactCheckTy = FunctionType::get(Type::VoidTy, FArg2, false);
  ExactCheck = M.getOrInsertFunction("exactcheck", ExactCheckTy);

  std::vector<const Type*> FArg4(1, VoidPtrType); //base
  FArg4.push_back(VoidPtrType); //result
  FArg4.push_back(Type::Int32Ty); //size
  FunctionType *ExactCheck2Ty = FunctionType::get(VoidPtrType, FArg4, false);
  ExactCheck2  = M.getOrInsertFunction("exactcheck2",  ExactCheck2Ty);

  std::vector<const Type *> FArg3(1, Type::Int32Ty);
  FArg3.push_back(VoidPtrType);
  FArg3.push_back(VoidPtrType);
  FunctionType *FunctionCheckTy = FunctionType::get(Type::VoidTy, FArg3, true);
  FunctionCheck = M.getOrInsertFunction("funccheck", FunctionCheckTy);

  std::vector<const Type*> FArg5(1, VoidPtrType);
  FArg5.push_back(VoidPtrType);
  FunctionType *GetActualValueTy = FunctionType::get(VoidPtrType, FArg5, false);
  GetActualValue=M.getOrInsertFunction("pchk_getActualValue", GetActualValueTy);

  std::vector<const Type*> FArg6(1, VoidPtrType);
  FArg6.push_back(VoidPtrType);
  FunctionType *StackFreeTy = FunctionType::get(VoidPtrType, FArg6, false);
  StackFree=M.getOrInsertFunction("poolunregister", StackFreeTy);
}

//
// Method: getDSGraph()
//
// Description:
//  Return the DSGraph for the given function.  This method automatically
//  selects the correct pass to query for the graph based upon whether we're
//  doing user-space or kernel analysis.
//
DSGraph &
InsertPoolChecks::getDSGraph(Function & F) {
#ifndef LLVA_KERNEL
  return paPass->getDSGraph(F);
#else  
  return TDPass->getDSGraph(F);
#endif  
}

DSNode* InsertPoolChecks::getDSNode(const Value *V, Function *F) {
#ifndef LLVA_KERNEL
  DSGraph &TDG = paPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
}

unsigned InsertPoolChecks::getDSNodeOffset(const Value *V, Function *F) {
#ifndef LLVA_KERNEL
  DSGraph &TDG = paPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  return TDG.getNodeForValue((Value *)V).getOffset();
}
#ifndef LLVA_KERNEL
Value *
InsertPoolChecks::getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI,
                                bool collapsed) {
#if 1
  //
  // JTC:
  //  If this function has a clone, then try to grab the original.
  //
  if (!(paPass->getFuncInfo(*F)))
  {
std::cerr << "PoolHandle: Getting original Function\n";
    F = paPass->getOrigFunctionFromClone(F);
    assert (F && "No Function Information from Pool Allocation!\n");
  }
#endif

  //
  // Get the DSNode for the value.
  //
  const DSNode *Node = getDSNode(V,F);
  if (!Node) {
    std::cerr << "JTC: Value " << *V << " has no DSNode!" << std::endl;
    return 0;
  }

  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
  const Type *PoolDescType = paPass->getPoolType();
  const Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);
  if (Node->isUnknownNode()) {
    //
    // FIXME:
    //  This should be in a top down pass or propagated like collapsed pools
    //  below .
    //
    if (!collapsed) {
#if 0
      assert(!getDSNodeOffset(V, F) && " we don't handle middle of structs yet\n");
#else
      if (getDSNodeOffset(V, F))
        std::cerr << "ERROR: we don't handle middle of structs yet"
                  << std::endl;
#endif
std::cerr << "JTC: PH: Null 1: " << *V << std::endl;
      return Constant::getNullValue(PoolDescPtrTy);
    }
  }

  Value * PH = paPass->getPool (Node, *F);
  map <Function *, set<Value *> > &
    CollapsedPoolPtrs = efPass->CollapsedPoolPtrs;
  
  if (PH) {
    // Check that the node pointed to by V in the TD DS graph is not
    // collapsed
    
    if (!collapsed && CollapsedPoolPtrs.count(F)) {
      Value *v = PH;
      if (CollapsedPoolPtrs[F].find(PH) != CollapsedPoolPtrs[F].end()) {
#ifdef DEBUG
        std::cerr << "Collapsed pools \n";
#endif
std::cerr << "JTC: PH: Null 2: " << *V << std::endl;
        return Constant::getNullValue(PoolDescPtrTy);
      } else {
        if (Argument * Arg = dyn_cast<Argument>(v))
          if ((Arg->getParent()) != F)
{
std::cerr << "JTC: PH: Null 3: " << *V << std::endl;
            return Constant::getNullValue(PoolDescPtrTy);
}
        return v;
      }
    } else {
      if (Argument * Arg = dyn_cast<Argument>(PH))
        if ((Arg->getParent()) != F)
{
std::cerr << "JTC: PH: Null 4: " << *V << std::endl;
          return Constant::getNullValue(PoolDescPtrTy);
}
      return PH;
    } 
  }

std::cerr << "JTC: Value " << *V << " not in PoolDescriptor List!" << std::endl;
  return 0;
}
#else
Value *InsertPoolChecks::getPoolHandle(const Value *V, Function *F) {
  DSGraph &TDG =  TDPass->getDSGraph(*F);
  const DSNode *Node = TDG.getNodeForValue((Value *)V).getNode();
  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  //  if (Node->isUnknownNode()) {
  //    return 0;
  //  }
  if ((TDG.getPoolDescriptorsMap()).count(Node)) 
    return (TDG.getPoolDescriptorsMap()[Node]->getMetaPoolValue());
  return 0;
}
#endif
}
