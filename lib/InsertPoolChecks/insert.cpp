#include "InsertPoolChecks.h"
#include "llvm/Instruction.h"
#include "llvm/Module.h"
using namespace llvm;
RegisterOpt<InsertPoolChecks> ipc("ipc", "insert runtime checks");

bool InsertPoolChecks::run(Module &M) {
  budsPass = &getAnalysis<CompleteBUDataStructures>();
  cuaPass = &getAnalysis<ConvertUnsafeAllocas>();
  paPass = &getAnalysis<PoolAllocate>();
  efPass = &getAnalysis<EmbeCFreeRemoval>();
  //add the new poolcheck prototype 
  addPoolCheckProto(M);
  //Replace old poolcheck with the new one 
  addPoolChecks(M);
  return true;
}
/* old version
      Function *origF = paPass->getOriginalFunction(F);
    const Value *OrigInst = GEP;
    if (!FI->NewToOldValueMap.empty()) {
      OrigInst = FI->NewToOldValueMap[GEP];
    }
    Value *PH = getPoolHandle(OrigInst, origF, *FI);
 */

void InsertPoolChecks::addPoolChecks(Module &M) {
  std::vector<Instruction *> & UnsafeGetElemPtrs = cuaPass->getUnsafeGetElementPtrsFromABC();
  std::vector<Instruction *>::const_iterator iCurrent = UnsafeGetElemPtrs.begin(), iEnd = UnsafeGetElemPtrs.end();
  for (; iCurrent != iEnd; ++iCurrent) {
    Instruction *GEP = *iCurrent;
    //we have the GetElementPtr
 
    Function *F = GEP->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFunctionInfo(F);
    Value *PH = getPoolHandle(GEP, F, *FI);
    Instruction *Casted = GEP;
    if (PH != 0) {
      if (!FI->ValueMap.empty()) {
	assert(FI->ValueMap[GEP] && "Instruction not in the value map \n");
	Instruction *temp = dyn_cast<Instruction>(FI->ValueMap[GEP]);
	assert(temp && " Instruction  not there in the Value map");
	Casted  = temp;
      }
      if (Casted->getType() != PointerType::get(Type::SByteTy)) {
	Casted = new CastInst(Casted,PointerType::get(Type::SByteTy),
			      (Casted)->getName()+".casted",(Casted)->getNext());
      }
      std::vector<Value *> args(1, PH);
      args.push_back(Casted);
      //Insert it
      CallInst * newCI = new CallInst(PoolCheck,args, "",Casted->getNext());
    }
  }
}

void InsertPoolChecks::addPoolCheckProto(Module &M) {
      const Type * VoidPtrType = PointerType::get(Type::SByteTy);
      const Type *PoolDescType = ArrayType::get(VoidPtrType, 5);
	//	StructType::get(make_vector<const Type*>(VoidPtrType, VoidPtrType,
	//                                               Type::UIntTy, Type::UIntTy, 0));
      const Type * PoolDescTypePtr = PointerType::get(PoolDescType);
      
      std::vector<const Type *> Arg(1, PoolDescTypePtr);
      Arg.push_back(VoidPtrType);
    FunctionType *PoolCheckTy =
      FunctionType::get(Type::VoidTy,Arg, false);
    PoolCheck = M.getOrInsertFunction("poolcheck", PoolCheckTy);

}

Value *InsertPoolChecks::getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI) {
  DSNode *Node = cuaPass->getDSNode(V,F);
  map <Function *, set<Value *> > &
    CollapsedPoolPtrs = efPass->CollapsedPoolPtrs;
  
      // Get the pool handle for this DSNode...
  std::map<DSNode*, Value*>::iterator I = FI.PoolDescriptors.find(Node);
  
  if (I != FI.PoolDescriptors.end()) {
    // Check that the node pointed to by V in the TD DS graph is not
    // collapsed 
    if (CollapsedPoolPtrs[F].find(I->second) !=
	CollapsedPoolPtrs[F].end()) {
      std::cerr << "Collapsed pools \n";
      return 0;
    } else {
      return I->second;
    } 
  }
  return 0;
}
     
