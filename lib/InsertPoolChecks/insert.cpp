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
RegisterOpt<InsertPoolChecks> ipc("safecode", "insert runtime checks");

// Options for Disabling the Insertion of Various Checks
cl::opt<bool> DisableLSChecks  ("disable-lschecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable Load/Store Checks"));
cl::opt<bool> DisableGEPChecks ("disable-gepchecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable GetElementPtr(GEP) Checks"));

// Options for where to insert various initialization code
cl::opt<string> InitFunctionName ("initfunc",
                                  cl::desc("Specify name of initialization "
                                           "function"),
                                  cl::value_desc("function name"));

// Pass Statistics
static Statistic<> NullChecks ("safecode",
                               "Poolchecks with NULL pool descriptor");
static Statistic<> FullChecks ("safecode",
                               "Poolchecks with non-NULL pool descriptor");
static Statistic<> MissChecks ("safecode",
                               "Poolchecks omitted due to bad pool descriptor");
static Statistic<> PoolChecks ("safecode", "Poolchecks Added");
static Statistic<> BoundChecks("safecode",
                               "Bounds checks inserted");

bool InsertPoolChecks::runOnModule(Module &M) {
  cuaPass = &getAnalysis<ConvertUnsafeAllocas>();
  //  budsPass = &getAnalysis<CompleteBUDataStructures>();
#ifndef LLVA_KERNEL  
  paPass = &getAnalysis<PoolAllocate>();
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
  Function *MainFunc = M.getMainFunction();
  if (MainFunc == 0 || MainFunc->isExternal()) {
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
    Function *PoolRegister = paPass->PoolRegister;
    BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
    while ((isa<CallInst>(InsertPt)) || isa<CastInst>(InsertPt) || isa<AllocaInst>(InsertPt) || isa<BinaryOperator>(InsertPt)) ++InsertPt;
    Instruction *I = InsertPt;
    if (PH) {
      Type *VoidPtrType = PointerType::get(Type::SByteTy); 
      Instruction *GVCasted = new CastInst(Argv,
					   VoidPtrType, Argv->getName()+"casted",InsertPt);
      const Type* csiType = Type::getPrimitiveType(Type::UIntTyID);
      Value *AllocSize = new CastInst(Argc,
				      csiType, Argc->getName()+"casted",InsertPt);
      AllocSize = BinaryOperator::create(Instruction::Mul, AllocSize,
					 ConstantUInt::get(csiType, 4), "sizetmp", InsertPt);
      CallInst *CI = new CallInst(PoolRegister,
				  make_vector(PH, AllocSize, GVCasted, 0),
				  "", InsertPt); 
      
    } else {
      std::cerr << "argv's pool descriptor is not present. \n";
      //	abort();
    }
    
  }
  //Now iterate over globals and register all the arrays
    Module::global_iterator GI = M.global_begin(), GE = M.global_end();
    for ( ; GI != GE; ++GI) {
      if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GI)) {
	Type *VoidPtrType = PointerType::get(Type::SByteTy); 
	Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
	Type *PoolDescPtrTy = PointerType::get(PoolDescType);
	if (GV->getType() != PoolDescPtrTy) {
	  DSGraph &G = equivPass->getGlobalsGraph();
	  DSNode *DSN  = G.getNodeForValue(GV).getNode();
	  if ((isa<ArrayType>(GV->getType()->getElementType())) || DSN->isNodeCompletelyFolded()) {
	    Value * AllocSize;
	    const Type* csiType = Type::getPrimitiveType(Type::UIntTyID);
	    if (const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType())) {
	      //std::cerr << "found global \n";
	      AllocSize = ConstantUInt::get(csiType,
 					    (AT->getNumElements() * TD->getTypeSize(AT->getElementType())));
	    } else {
	      AllocSize = ConstantUInt::get(csiType, TD->getTypeSize(GV->getType()));
	    }
	    Function *PoolRegister = paPass->PoolRegister;
	    BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
	    //skip the calls to poolinit
	    while ((isa<CallInst>(InsertPt)) || isa<CastInst>(InsertPt) || isa<AllocaInst>(InsertPt) || isa<BinaryOperator>(InsertPt)) ++InsertPt;
	    
            Instruction *ipt = InsertPt;
	    std::map<const DSNode *, Value *>::iterator I = paPass->GlobalNodes.find(DSN);
	    if (I != paPass->GlobalNodes.end()) {
	      Value *PH = I->second;
	      Instruction *GVCasted = new CastInst(GV,
						   VoidPtrType, GV->getName()+"casted",InsertPt);
	      CallInst *CI = new CallInst(PoolRegister,
					  make_vector(PH, AllocSize, GVCasted, 0),
					  "", InsertPt); 
	    } else {
	      std::cerr << "pool descriptor not present \n ";
	      abort();
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
    // Get the pool handle associated with this pointer.  If there is no pool
    // handle, use a NULL pointer value and let the runtime deal with it.
    Value *PH = getPoolHandle(V, F);
#ifdef DEBUG
std::cerr << "LLVA: addLSChecks: Pool " << PH << " Node " << Node << std::endl;
#endif
    if (!PH) {
      PH = Constant::getNullValue(PointerType::get(Type::SByteTy));
      ++NullChecks;
    } else {
#if 1
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
#else
          ++FullChecks;
#endif
    }      
    // Create instructions to cast the checked pointer and the checked pool
    // into sbyte pointers.
    CastInst *CastVI = 
      new CastInst(V, 
		   PointerType::get(Type::SByteTy), "node.lscasted", I);
    CastInst *CastPHI = 
      new CastInst(PH, 
		   PointerType::get(Type::SByteTy), "poolhandle.lscasted", I);

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

      if (CallInst *CI = dyn_cast<CallInst>(I)) {
	// GEt the globals list corresponding to the node
	return;
	std::vector<Function *> FuncList;
	Node->addFullFunctionList(FuncList);
	std::vector<Function *>::iterator flI= FuncList.begin(), flE = FuncList.end();
	unsigned num = FuncList.size();
	if (flI != flE) {
	  const Type* csiType = Type::getPrimitiveType(Type::UIntTyID);
	  Value *NumArg = ConstantUInt::get(csiType, num);	
					 
	  CastInst *CastVI = 
	    new CastInst(Vnew, 
			 PointerType::get(Type::SByteTy), "casted", I);
	
	  std::vector<Value *> args(1, NumArg);
	  args.push_back(CastVI);
	  for (; flI != flE ; ++flI) {
	    Function *func = *flI;
	    CastInst *CastfuncI = 
	      new CastInst(func, 
			   PointerType::get(Type::SByteTy), "casted", I);
	    args.push_back(CastfuncI);
	  }
	  new CallInst(FunctionCheck, args,"", I);
	}
      } else {


	CastInst *CastVI = 
	  new CastInst(Vnew, 
		       PointerType::get(Type::SByteTy), "casted", I);
	CastInst *CastPHI = 
	  new CastInst(PH, 
		       PointerType::get(Type::SByteTy), "casted", I);
	std::vector<Value *> args(1,CastPHI);
	args.push_back(CastVI);
	
	new CallInst(PoolCheck,args,"", I);
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
	//we need to get the LI from the original function
	Value *P = LI->getPointerOperand();
	if (isClonedFunc) {
	  assert(FI->NewToOldValueMap.count(LI) && " not in the value map \n");
	  const LoadInst *temp = dyn_cast<LoadInst>(FI->NewToOldValueMap[LI]);
	  assert(temp && " Instruction  not there in the NewToOldValue map");
	  const Value *Ptr = temp->getPointerOperand();
	  addLSChecks(P, Ptr, LI, Forig);
	} else {
	  addLSChecks(P, P, LI, Forig);
	}
      } else if (StoreInst *SI = dyn_cast<StoreInst>(&*I)) {
	Value *P = SI->getPointerOperand();
	if (isClonedFunc) {
	  assert(FI->NewToOldValueMap.count(SI) && " not in the value map \n");
	  const StoreInst *temp = dyn_cast<StoreInst>(FI->NewToOldValueMap[SI]);
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
	    assert(FI->NewToOldValueMap.count(CI) && " not in the value map \n");
	    const CallInst *temp = dyn_cast<CallInst>(FI->NewToOldValueMap[CI]);
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
  std::vector<Instruction *> & UnsafeGetElemPtrs = cuaPass->getUnsafeGetElementPtrsFromABC();
  std::vector<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs.begin(), iEnd = UnsafeGetElemPtrs.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    // We have the GetElementPtr
    if (!isa<GetElementPtrInst>(*iCurrent)) {
      // Then this must be some trusted call we cant prove safety
      //      std::cerr << "WARNING : DID NOT HANDLE trusted call  \n";
      //      (*iCurrent)->dump();
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
    if (!FI->ValueMap.empty()) {
      assert(FI->ValueMap.count(GEP) && "Instruction not in the value map \n");
      Instruction *temp = dyn_cast<Instruction>(FI->ValueMap[GEP]);
      assert(temp && " Instruction  not there in the Value map");
      Casted  = temp;
    }
    if (GetElementPtrInst *GEPNew = dyn_cast<GetElementPtrInst>(Casted)) {
      Value *PH = getPoolHandle(GEP, F, *FI);
      if (PH && isa<ConstantPointerNull>(PH)) continue;
      if (!PH) {
        Value *PointerOperand = GEPNew->getPointerOperand();
        if (ConstantExpr *cExpr = dyn_cast<ConstantExpr>(PointerOperand)) {
          if (cExpr->getOpcode() == Instruction::Cast)
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
              if (secOp->getType() != Type::UIntTy) {
                secOp = new CastInst(secOp, Type::UIntTy,
                                     secOp->getName()+".ec.casted", Casted);
              }

              const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
              std::vector<Value *> args(1,secOp);
              args.push_back(ConstantSInt::get(csiType,AT->getNumElements()));
              CallInst *newCI = new CallInst(ExactCheck,args,"", Casted);
              DEBUG(std::cerr << "Inserted exact check call Instruction \n");
              continue;
            } else if (GEPNew->getNumOperands() == 3) {
              if (ConstantSInt *COP = dyn_cast<ConstantSInt>(GEPNew->getOperand(1))) {
                // FIXME: assuming that the first array index is 0
                assert((COP->getRawValue() == 0) && "non zero array index\n");
                Value * secOp = GEPNew->getOperand(2);
                if (secOp->getType() != Type::UIntTy) {
                  secOp = new CastInst(secOp, Type::UIntTy,
                                       secOp->getName()+".ec2.casted", Casted);
                }
                std::vector<Value *> args(1,secOp);
                const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
                args.push_back(ConstantSInt::get(csiType,AT->getNumElements()));
                CallInst *newCI = new CallInst(ExactCheck, args, "",
                                               Casted->getNext());
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
        if (Casted->getType() != PointerType::get(Type::SByteTy)) {
          Casted = new CastInst(Casted,PointerType::get(Type::SByteTy),
                                (Casted)->getName()+".pc.casted",
                                (Casted)->getNext());
        }
        std::vector<Value *> args(1, PH);
        args.push_back(Casted);
        // Insert it
        CallInst * newCI = new CallInst(PoolCheck,args, "",Casted->getNext());
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

    //
    // If the pool handle is a NULL pointer, don't bother inserting the
    // check.
    //
#if 0
    if (PH && isa<ConstantPointerNull>(PH)) {
      ++NullChecks;
      continue;
    }
#endif

    DSGraph & TDG = TDPass->getDSGraph(*F);
    DSNode * Node = TDG.getNodeForValue(GEP).getNode();

    DEBUG(std::cerr << "LLVA: addGEPChecks: Pool " << PH << " Node ");
    DEBUG(std::cerr << Node << std::endl);

    if (!PH) {
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
            if (secOp->getType() != Type::UIntTy) {
              secOp = new CastInst(secOp, Type::UIntTy,
                                   secOp->getName()+".ec3.casted", Casted);
            }

            std::vector<Value *> args(1,secOp);
            const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
            args.push_back(ConstantSInt::get(csiType,AT->getNumElements()));
            CallInst *newCI = new CallInst(ExactCheck,args,"", Casted);
            ++BoundChecks;
            //	    DEBUG(std::cerr << "Inserted exact check call Instruction \n");
            continue;
          } else if (GEPNew->getNumOperands() == 3) {
            if (ConstantSInt *COP = dyn_cast<ConstantSInt>(GEPNew->getOperand(1))) {
              //FIXME assuming that the first array index is 0
              assert((COP->getRawValue() == 0) && "non zero array index\n");
              Value * secOp = GEPNew->getOperand(2);
              if (secOp->getType() != Type::UIntTy) {
                secOp = new CastInst(secOp, Type::UIntTy,
                                     secOp->getName()+".ec4.casted", Casted);
              }
              std::vector<Value *> args(1,secOp);
              const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
              args.push_back(ConstantSInt::get(csiType,AT->getNumElements()));
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
          }
        }
        DEBUG(std::cerr << " Global variable ok \n");
      }

      //      These must be real unknowns and they will be handled anyway
      //      std::cerr << " WARNING, DID NOT HANDLE   \n";
      //      (*iCurrent)->dump();
#if 0
      PH = Constant::getNullValue(PointerType::get(Type::SByteTy));
#endif
    } else {
      //
      // Determine whether the pool handle dominates the pool check.
      // If not, then don't insert it.
      //
#if 1
      //
      // Only add the pool check if the pool is a global value or it
      // belongs to the same basic block.
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
          for (IP=IPH; (IP->isTerminator()) || (IP == Casted); IP=IP->getNext()) {
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
#else
          ++FullChecks;
#endif
    }

    //
    // We cannot insert an exactcheck().  Insert a pool check.
    //
    // FIXME:
    //  Currently, we cannot register stack or global memory with pools.  If
    //  the node is from alloc() or is a global, do not insert a poolcheck.
    // 
    if ((!PH) || (Node->isAllocaNode()) || (Node->isGlobalNode())) {
      ++NullChecks;
      PH = Constant::getNullValue(PointerType::get(Type::SByteTy));
      DEBUG(std::cerr << "missing a GEP check for" << GEP << "alloca case?\n");
    }

    if (1) {
      if (Casted->getType() != PointerType::get(Type::SByteTy)) {
        Casted = new CastInst(Casted,PointerType::get(Type::SByteTy),
        (Casted)->getName()+".pc2.casted",(Casted)->getNext());
      }

      Instruction *CastedPH = new CastInst(PH,
                                           PointerType::get(Type::SByteTy),
                                           "ph",(Casted)->getNext());
      std::vector<Value *> args(1, CastedPH);
      args.push_back(Casted);
      //Insert it
      CallInst * newCI = new CallInst(PoolCheck,args, "",CastedPH->getNext());
      //      DEBUG(std::cerr << "inserted instruction \n");
    }
#endif    
  }
}

void InsertPoolChecks::addPoolCheckProto(Module &M) {
  const Type * VoidPtrType = PointerType::get(Type::SByteTy);
  /*
  const Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  //	StructType::get(make_vector<const Type*>(VoidPtrType, VoidPtrType,
  //                                               Type::UIntTy, Type::UIntTy, 0));
  const Type * PoolDescTypePtr = PointerType::get(PoolDescType);
  */  

  std::vector<const Type *> Arg(1, VoidPtrType);
  Arg.push_back(VoidPtrType);
  FunctionType *PoolCheckTy =
    FunctionType::get(Type::VoidTy,Arg, false);
  PoolCheck = M.getOrInsertFunction("poolcheck", PoolCheckTy);

  std::vector<const Type *> FArg2(1, Type::UIntTy);
  FArg2.push_back(Type::IntTy);
  FunctionType *ExactCheckTy = FunctionType::get(Type::VoidTy, FArg2, false);
  ExactCheck = M.getOrInsertFunction("exactcheck", ExactCheckTy);

  std::vector<const Type *> FArg3(1, Type::UIntTy);
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
  const DSNode *Node = getDSNode(V,F);
  // Get the pool handle for this DSNode...
  //  assert(!Node->isUnknownNode() && "Unknown node \n");
  Type *VoidPtrType = PointerType::get(Type::SByteTy); 
  Type *PoolDescType = ArrayType::get(VoidPtrType, 50);
  Type *PoolDescPtrTy = PointerType::get(PoolDescType);
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
	return v;
      }
    } else {
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
    return TDG.getPoolDescriptorsMap()[Node];
  return 0;
}
#endif
