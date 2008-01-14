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
}

namespace llvm {
bool InsertPoolChecks::runOnModule(Module &M) {
  abcPass = getAnalysisToUpdate<ArrayBoundsCheck>();
  //  budsPass = &getAnalysis<CompleteBUDataStructures>();
#ifndef LLVA_KERNEL  
  paPass = getAnalysisToUpdate<PoolAllocate>();
  assert (paPass && "Pool Allocation Transform *must* be run first!");
  equivPass = &(paPass->getECGraphs());
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
  //Replace old poolcheck with the new one 
  addPoolChecks(M);

  //
  // Update the statistics.
  //
  PoolChecks = NullChecks + FullChecks;
  
  return true;
}

#ifndef LLVA_KERNEL
void InsertPoolChecks::registerGlobalArraysWithGlobalPools(Module &M) {
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    std::cerr << "Cannot do array bounds check for this program"
	      << "no 'main' function yet!\n";
    abort();
  }
  //First register, argc and argv
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
    while ((isa<CallInst>(InsertPt)) || isa<CastInst>(InsertPt) || isa<AllocaInst>(InsertPt) || isa<BinaryOperator>(InsertPt)) ++InsertPt;
    if (PH) {
      Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
      Instruction *GVCasted = CastInst::createPointerCast(Argv,
					   VoidPtrType, Argv->getName()+"casted",InsertPt);
      const Type* csiType = Type::Int32Ty;
      Value *AllocSize = CastInst::createZExtOrBitCast(Argc,
				      csiType, Argc->getName()+"casted",InsertPt);
      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 ConstantInt::get(csiType, 4), "sizetmp", InsertPt);
      std::vector<Value *> args = make_vector (PH, AllocSize, GVCasted);
      new CallInst(PoolRegister, args.begin(), args.end(), "", InsertPt); 
      
    } else {
      std::cerr << "argv's pool descriptor is not present. \n";
      //	abort();
    }
    
  }
  //Now iterate over globals and register all the arrays
    Module::global_iterator GI = M.global_begin(), GE = M.global_end();
    for ( ; GI != GE; ++GI) {
      if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GI)) {
	Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
	Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
	Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);
	if (GV->getType() != PoolDescPtrTy) {
	  DSGraph &G = equivPass->getGlobalsGraph();
	  DSNode *DSN  = G.getNodeForValue(GV).getNode();
	  if ((isa<ArrayType>(GV->getType()->getElementType())) || DSN->isNodeCompletelyFolded()) {
	    Value * AllocSize;
	    const Type* csiType = Type::Int32Ty;
	    if (const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType())) {
	      //std::cerr << "found global \n";
	      AllocSize = ConstantInt::get(csiType,
 					    (AT->getNumElements() * TD->getABITypeSize(AT->getElementType())));
	    } else {
	      AllocSize = ConstantInt::get(csiType, TD->getABITypeSize(GV->getType()));
	    }
	    Constant *PoolRegister = paPass->PoolRegister;
	    BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
	    //skip the calls to poolinit
	    while ((isa<CallInst>(InsertPt)) || isa<CastInst>(InsertPt) || isa<AllocaInst>(InsertPt) || isa<BinaryOperator>(InsertPt)) ++InsertPt;
	    
	    std::map<const DSNode *, Value *>::iterator I = paPass->GlobalNodes.find(DSN);
	    if (I != paPass->GlobalNodes.end()) {
	      Value *PH = I->second;
	      Instruction *GVCasted = CastInst::createPointerCast(GV,
						   VoidPtrType, GV->getName()+"casted",InsertPt);
        std::vector<Value *> args = make_vector(PH, AllocSize, GVCasted, 0);
	      new CallInst(PoolRegister, args.begin(), args.end(), "", InsertPt); 
	    } else {
	      std::cerr << "pool descriptor not present for " << *GV << std::endl;
#if 0
	      abort();
#endif
	    }
	  }
	}
      }
    }
  }
#endif

void InsertPoolChecks::addPoolChecks(Module &M) {
  if (!DisableGEPChecks) addGetElementPtrChecks(M);
  if (!DisableLSChecks)  addLoadStoreChecks(M);
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
    Value *PH = getPoolHandle(V, F);
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
    new CallInst(PoolCheck,args,"", I);
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
  } else {
    if (PH && isa<ConstantPointerNull>(PH)) {
      //we have a collapsed/Unknown pool
      Value *PH = getPoolHandle(V, F, *FI, true); 

      if (dyn_cast<CallInst>(I)) {
	// GEt the globals list corresponding to the node
	return;
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
	  new CallInst(FunctionCheck, args.begin(), args.end(), "", I);
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
	
	new CallInst(PoolCheck,args.begin(), args.end(), "", I);
      }
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

void InsertPoolChecks::addGetElementPtrChecks(Module &M) {
  std::vector<Instruction *> & UnsafeGetElemPtrs = abcPass->UnsafeGetElemPtrs;
  std::vector<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs.begin(), iEnd = UnsafeGetElemPtrs.end();
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
          new CallInst(PoolCheckArray,args,"", InsertPt);
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
          new CallInst(PoolCheckArray,args,"", InsertPt);
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
              new CallInst(ExactCheck,args.begin(), args.end(), "", Casted);
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
                new CallInst(ExactCheck, args.begin(), args.end(), "", getNextInst(Casted));
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
        if (Casted->getType() != PointerType::getUnqual(Type::Int8Ty)) {
          Casted = CastInst::createPointerCast(Casted,PointerType::getUnqual(Type::Int8Ty),
                                (Casted)->getName()+".pc.casted",
                                getNextInst(Casted));
        }
        std::cerr << "PH = " << *PH << std::endl;
        Instruction *CastedPH = CastInst::createPointerCast(PH,
                                             PointerType::getUnqual(Type::Int8Ty),
                                             "ph",getNextInst(Casted));
        std::vector<Value *> args(1, CastedPH);
        args.push_back(Casted);
        // Insert it
        new CallInst(PoolCheck,args.begin(), args.end(), "",getNextInst(CastedPH));
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
          CallInst *newCI = new CallInst(ExactCheck,args,"", Casted);
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
            CallInst *newCI = new CallInst(ExactCheck,args,"", Casted->getNext());
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
    // FIXME:
    //  Currently, we cannot register stack or global memory with pools.  If
    //  the node is from alloc() or is a global, do not insert a poolcheck.
    // 
#if 0
    if ((!PH) || (Node->isAllocaNode()) || (Node->isGlobalNode())) {
#else
    if (!PH) {
#endif
      ++NullChecks;
      if (!PH) ++MissedNullChecks;
#if 0
      if (Node->isAllocaNode()) ++MissedStackChecks;
      if (Node->isGlobalNode()) ++MissedGlobalChecks;
#endif
      // Don't bother to insert the NULL check unless the user asked
      if (!EnableNullChecks)
        continue;
      PH = Constant::getNullValue(PointerType::getUnqual(Type::Int8Ty));
      DEBUG(std::cerr << "missing a GEP check for" << GEP << "alloca case?\n");
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
    // If this is a complete node, insert a poolcheck.
    // If this is an icomplete node, insert a poolcheckarray.
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
    if (Node->isIncomplete()) {
      std::vector<Value *> args(1, CastedPH);
      args.push_back(CastedPointerOperand);
      args.push_back(Casted);
      CallInst * newCI = new CallInst(PoolCheckArray,args, "",InsertPt);
    } else {
      std::vector<Value *> args(1, CastedPH);
      args.push_back(Casted);
      CallInst * newCI = new CallInst(PoolCheck,args, "",InsertPt);
    }
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

  std::vector<const Type *> Arg(1, VoidPtrType);
  Arg.push_back(VoidPtrType);
  FunctionType *PoolCheckTy =
    FunctionType::get(Type::VoidTy,Arg, false);
  PoolCheck = M.getOrInsertFunction("poolcheck", PoolCheckTy);

  std::vector<const Type *> Arg2(1, VoidPtrType);
  Arg2.push_back(VoidPtrType);
  Arg2.push_back(VoidPtrType);
  FunctionType *PoolCheckArrayTy =
    FunctionType::get(Type::VoidTy,Arg2, false);
  PoolCheckArray = M.getOrInsertFunction("poolcheckarray", PoolCheckArrayTy);
  
  std::vector<const Type *> FArg2(1, Type::Int32Ty);
  FArg2.push_back(Type::Int32Ty);
  FunctionType *ExactCheckTy = FunctionType::get(Type::VoidTy, FArg2, false);
  ExactCheck = M.getOrInsertFunction("exactcheck", ExactCheckTy);

  std::vector<const Type *> FArg3(1, Type::Int32Ty);
  FArg3.push_back(VoidPtrType);
  FArg3.push_back(VoidPtrType);
  FunctionType *FunctionCheckTy = FunctionType::get(Type::VoidTy, FArg3, true);
  FunctionCheck = M.getOrInsertFunction("funccheck", FunctionCheckTy);
  
}

DSNode* InsertPoolChecks::getDSNode(const Value *V, Function *F) {
#ifndef LLVA_KERNEL
  DSGraph &TDG = equivPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  DSNode *DSN = TDG.getNodeForValue((Value *)V).getNode();
  return DSN;
}

unsigned InsertPoolChecks::getDSNodeOffset(const Value *V, Function *F) {
#ifndef LLVA_KERNEL
  DSGraph &TDG = equivPass->getDSGraph(*F);
#else  
  DSGraph &TDG = TDPass->getDSGraph(*F);
#endif  
  return TDG.getNodeForValue((Value *)V).getOffset();
}
#ifndef LLVA_KERNEL
Value *InsertPoolChecks::getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed) {
#if 1
  //
  // JTC:
  //  If this function has a clone, then try to grab the original.
  //
  if (!(paPass->getFuncInfo(*F)))
  {
std::cerr << "PoolHandle: Getting original Function\n";
    F = paPass->getOrigFunctionFromClone(F);
  }
#endif
  const DSNode *Node = getDSNode(V,F);
  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  Type *VoidPtrType = PointerType::getUnqual(Type::Int8Ty); 
  Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  Type *PoolDescPtrTy = PointerType::getUnqual(PoolDescType);
  if (!Node) {
    return 0; //0 means there is no isse with the value, otherwise there will be a callnode
  }
  if (Node->isUnknownNode()) {
    //FIXME this should be in a top down pass or propagated like collapsed pools below 
    if (!collapsed) {
      assert(!getDSNodeOffset(V, F) && " we don't handle middle of structs yet\n");
      return Constant::getNullValue(PoolDescPtrTy);
    }
  }
  std::map<const DSNode*, Value*>::iterator I = FI.PoolDescriptors.find(Node);
  map <Function *, set<Value *> > &
    CollapsedPoolPtrs = efPass->CollapsedPoolPtrs;
  
  if (I != FI.PoolDescriptors.end()) {
    // Check that the node pointed to by V in the TD DS graph is not
    // collapsed
    
    if (!collapsed && CollapsedPoolPtrs.count(F)) {
      Value *v = I->second;
      if (CollapsedPoolPtrs[F].find(I->second) !=
	  CollapsedPoolPtrs[F].end()) {
#ifdef DEBUG
	std::cerr << "Collapsed pools \n";
#endif
	return Constant::getNullValue(PoolDescPtrTy);
      } else {
        if (Argument * Arg = dyn_cast<Argument>(v))
          if ((Arg->getParent()) != F)
            return Constant::getNullValue(PoolDescPtrTy);
	return v;
      }
    } else {
      if (Argument * Arg = dyn_cast<Argument>(I->second))
        if ((Arg->getParent()) != F)
          return Constant::getNullValue(PoolDescPtrTy);
      return I->second;
    } 
  }
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
