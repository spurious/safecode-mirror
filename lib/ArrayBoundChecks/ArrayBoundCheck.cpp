//===- ArrayBoundCheck.cpp - ArrayBounds Checking (Omega)----------------===//
//
// requires piNodeinsertion pass before
// Now we use the control dependence 
// FIXME : handle some aliases. 
//===----------------------------------------------------------------------===//
#include <unistd.h>
#include "utils/fdstream.h"
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/BasicBlock.h"
#include "ArrayBoundsCheck.h"
#include "llvm/Support/InstIterator.h"
#include "Support/StringExtras.h"
#include "llvm/Constants.h"
#include "llvm/Analysis/InductionVariable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "omega.h"
#include "Support/VectorExtras.h"
#include "llvm//Analysis/DSGraph.h"
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <fcntl.h>

#define OMEGA_TMP_FILE "omega.ip"
#define OMEGA_TMP_INCLUDE_FILE "omega_include.ip"
#define REGION_INIT "RInit"
#define REGION_MALLOC "RMalloc"
#define REGION_FREE "RFree"

using namespace llvm;
using namespace ABC;

 std::ofstream includeOut(OMEGA_TMP_INCLUDE_FILE);
 //these are filled from preprocess pass, since they require fn passes
 extern IndVarMap indMap; 
  extern ExitNodeMap enMap;
  extern PostDominanceFrontier* pdf;
  extern DominatorSet* ds;
  extern PostDominatorSet* pds;
 
  static  unsigned countA = 0;
  //Interprocedural ArrayBoundsCheck pass
  RegisterOpt<ArrayBoundsCheck> abc1("abc1","Array Bounds Checking pass");


  void ArrayBoundsCheck::initialize(Module &M) {
    KnownFuncDB.insert("puts");
    KnownFuncDB.insert("memcpy"); //need to add the extra checks 
    //    KnownFuncDB.insert("fscanf");
    KnownFuncDB.insert("strcpy"); //need to add the extra checks 
    KnownFuncDB.insert("strlen");
    KnownFuncDB.insert("strcmp");
    KnownFuncDB.insert("strtol");
    KnownFuncDB.insert("fopen");
    KnownFuncDB.insert("fwrite");
    KnownFuncDB.insert("fgetc");
    KnownFuncDB.insert("getc");
    KnownFuncDB.insert("read");
    KnownFuncDB.insert("fread");
    KnownFuncDB.insert("feof");
    KnownFuncDB.insert("fputc");
    KnownFuncDB.insert("qsort"); //need to add the extra checks
    KnownFuncDB.insert("atoi");
    KnownFuncDB.insert("exit");
    KnownFuncDB.insert("perror");
    KnownFuncDB.insert("fprintf");
  // Get poolCheck function...
    /*
   const Type * voidPtrType = PointerType::get(Type::SByteTy);
   std::vector<const Type*> PDArgs(1, voidPtrType);
    FunctionType *PoolCheckTy =
      FunctionType::get(Type::VoidTy, PDArgs, false);
    PoolCheck = M.getOrInsertFunction("poolchecktmp", PoolCheckTy);

	Instruction *Casted = maI;
	if (Casted->getType() != PointerType::get(Type::SByteTy)) {
	  Casted = new CastInst(maI,PointerType::get(Type::SByteTy),
				(maI)->getName()+".casted",(maI)->getNext());
	}
	CallInst * CI = new CallInst(PoolCheck, Casted,"",Casted->getNext());
    */
  }
  
  void ArrayBoundsCheck::outputDeclsForOmega(Module& M) {
    for (Module::iterator FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
      Function *F = FI;
      includeOut << "symbolic   Unknown;\n";
      includeOut << "symbolic   argc;\n";
      includeOut << "symbolic   argv;\n";
      //      if (!(F->isExternal()))
      includeOut << "symbolic " << getValueName(F) <<"; \n";
      for (Function::ArgumentListType::iterator aI = F->getArgumentList().begin(),
	     aE = F->getArgumentList().end(); aI != aE; ++aI) {
	includeOut << "symbolic   ";
	includeOut << getValueName((aI));
	includeOut << ";\n";
      }
      for (Module::giterator gI = M.gbegin(), gE = M.gend(); gI != gE; ++gI) {
	includeOut << "symbolic   ";
	includeOut << getValueName((gI));	
	includeOut << ";\n";
	if (const ArrayType *AT = dyn_cast<ArrayType>(gI->getType()->getElementType())) {
	  printarraytype(getValueName(gI), AT);
	}
      }
      
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
	if ((*I)->getType() != Type::VoidTy) {
	  includeOut << "symbolic   ";
	  includeOut << getValueName(*I);	
	  includeOut << ";\n";
	  if (AllocationInst *AI = dyn_cast<AllocationInst>(*I)) {
	    //FIXME this should be really alloca (for local variables)
	    //This can not be malloc, if array bounds check is run just after
	    //bytecode is generated. But if other passes introduce mallocInsts
	    //we should take care of them just like the allocas 


	    //We have to see the dimension of the array that this alloca is
	    //pointing to
	    //If the allocation is done by constant, then its a constant array
	    // else its a normal alloca which we already have taken care of  
	    if (const ArrayType *AT = dyn_cast<ArrayType>(AI->getType()->getElementType())) {
	      printarraytype(getValueName(*I), AT);
	    }
	  } //Note that RMalloc unlike alloca cannot take an array type constant
	  //Array type constant is only taken by alloca for the local variables
	} 
    }
  }



  string ArrayBoundsCheck::getValueName(const Value *V) {
    return getValueNames(V, Table);
  }

  void ArrayBoundsCheck::printarraytype(string var,const ArrayType  *T) {
    string var1 = var + "_i";
    includeOut << "symbolic   " << var1 << ";\n";
    if (const ArrayType *AT = dyn_cast<ArrayType>(T->getElementType())) {
      printarraytype(var1,AT);
    }
  }

  //This is an auxillary function used by getConstraints
  //gets the constraints on the return value interms of its arguments
  // and ands it with the existing rootp!
  void ArrayBoundsCheck::getReturnValueConstraints(const CallInst *CI,ABCExprTree **rootp) {
    // out << "in return value constraints \n";
    //and the return 
    if (const Function* pf = dyn_cast<Function>(CI->getOperand(0))) {
      //we have to get the exit node for pf
      if (KnownFuncDB.find(CI->getOperand(0)->getName()) != KnownFuncDB.end()) {      
	if (!(pf->isExternal())) {
	  BasicBlock *bb = enMap[pf];
	  getConstraints(bb->getTerminator(),rootp);
	  /*
	if (bb->getTerminator()->getType()->getPrimitiveID() != Type::VoidTyID) {
	out << "in return value constraints for" << bb->getTerminator() << "\n";
	}
	  */
	}
      }
    }
  }

  void ArrayBoundsCheck::addControlDependentConditions(BasicBlock *currentBlock, ABCExprTree **rootp) {
    PostDominanceFrontier::const_iterator it = pdf->find(currentBlock);
    if (it != pdf->end()) {
      const PostDominanceFrontier::DomSetType &S = it->second;
      if (S.size() > 0) {
	PostDominanceFrontier::DomSetType::iterator pCurrent = S.begin(), pEnd = S.end();
	//check if it is control dependent on only one node.
	//If it is control dependent on only one node.
	//If it not, then there must be only one that dominates this node and
	//the rest should be dominated by this node.
	//or this must dominate every other node (incase of do while)
	bool dominated = false; 
	bool rdominated = true; //to check if this dominates every other node
	for (; pCurrent != pEnd; ++pCurrent) {
	  BasicBlock *debugBlock = *pCurrent;
	  if (*pCurrent == currentBlock) {
	    rdominated = rdominated & true;
	    continue;
	  }
	  if (!dominated) {
	    if (ds->dominates(*pCurrent, currentBlock)) {
	      dominated = true;
	      rdominated = false;
	      continue;
	    }
	  }
	  if (ds->dominates(currentBlock, *pCurrent)) {
	    rdominated = rdominated & true;
	    continue;
	  } else {
	    //	    out << "In function " << currentBlock->getParent()->getName();
	    //	    out << "for basic block " << currentBlock->getName();
	    //	    out << "Something wrong .. non affine or unstructured control flow ??\n";
	    dominated = false;
	    break;
	  }
	}
	if ((dominated) || (rdominated)) {
	  //Now we are sure that the control dominance is proper
	  //i.e. it doesn't have unstructured control flow 
	  
	  PostDominanceFrontier::DomSetType::iterator pdCurrent = S.begin(), pdEnd = S.end();
	  for (; pdCurrent != pdEnd; ++pdCurrent) {
	    BasicBlock *CBB = *pdCurrent;
	    if (DoneList.find(CBB) == DoneList.end()) {
	      TerminatorInst *TI = CBB->getTerminator();
	      if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
		for (unsigned index = 0; index < BI->getNumSuccessors(); ++index) {
		  BasicBlock * succBlock = BI->getSuccessor(index);
		  if (pds->dominates(currentBlock, succBlock)) {
		    //		  if (BI->getSuccessor(index) == currentBlock) {
		    //FIXME It need not be Successor, we need to check for post dominance
		    
		    DoneList.insert(CBB);
		    addControlDependentConditions(CBB,rootp);
		    addBranchConstraints(BI, BI->getSuccessor(index), rootp);
		    break;
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  }

  //adds constraints for known functions 
  void ArrayBoundsCheck::addConstraintsForKnownFunctions(CallInst *CI, ABCExprTree **rootp) {
    string funcName = CI->getOperand(0)->getName();
    if (funcName == "memcpy") {
      string var = getValueName(CI->getOperand(1));
      LinearExpr *l1 = new LinearExpr(CI->getOperand(2),*Table);
      Constraint *c1 = new Constraint(var,l1,">=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"||");
      getConstraints(CI->getOperand(1), rootp);
      getConstraints(CI->getOperand(2), rootp);
    } else if (funcName == "strlen") {
      string var = getValueName(CI);
      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
      const ConstantSInt * signedzero = ConstantSInt::get(csiType,0);
      
      Constraint *c = new Constraint(var, new LinearExpr(signedzero, *Table),">=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");


      string var1 = var +" + 1";

      LinearExpr *l1 = new LinearExpr(CI->getOperand(1),*Table);
      Constraint *c1 = new Constraint(var1,l1,"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(1), rootp);
    } else if (funcName == "read") {
      //FIXME we also need to check that the buf size is less than
      //the bytes reading
      string var = getValueName(CI);
      LinearExpr *l1 = new LinearExpr(CI->getOperand(3),*Table);
      Constraint *c1 = new Constraint(var,l1,"<=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(3), rootp);
      
    } else if (funcName == "fread") {
      //FIXME we also need to check that the buf size is less than
      //the bytes reading
      string var = getValueName(CI);
      LinearExpr *l1 = new LinearExpr(CI->getOperand(2),*Table);
      LinearExpr *l2 = new LinearExpr(CI->getOperand(3),*Table);
      l2->mulLinearExpr(l1);
      Constraint *c1 = new Constraint(var,l2,"<=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(3), rootp);
      getConstraints(CI->getOperand(2), rootp);
      
    } else {
      //      out << funcName << " is not supported yet \n";
    }
  }


  void ArrayBoundsCheck::getConstraints(Value *v, ABCExprTree **rootp) {
    string tempName1 = getValueName(v);
    LinearExpr *letemp1 = new LinearExpr(v,*Table);
    Constraint* ctemp1 = new Constraint(tempName1,letemp1,"=");
    ABCExprTree* abctemp1 = new ABCExprTree(ctemp1);
    getConstraintsInternal(v,&abctemp1);
    *rootp = new ABCExprTree(*rootp, abctemp1, "&&");
  }
  
  //Get Constraints on a value v, this assumes that the Table is correctly set
  //for the function that is cal ling this 
  void ArrayBoundsCheck::getConstraintsInternal(Value *v, ABCExprTree **rootp) {
    string var;
    LinearExpr *et;
    if ( Instruction *I = dyn_cast<Instruction>(v)) {
    
      Function* func = I->getParent()->getParent();
      BasicBlock * currentBlock = I->getParent();

      //Here we need to add the post dominator stuff if necessary
      addControlDependentConditions(currentBlock, rootp);

      if (!isa<ReturnInst>(I)) {
	var = getValueName(I);
      } else {
	var = getValueName(func);
      }
      if (fMap[func]->inLocalConstraints(I)) { //checking the cache
	if (fMap[func]->getLocalConstraint(I) != 0) {
	  *rootp = new ABCExprTree(*rootp, fMap[func]->getLocalConstraint(I),"&&");
	}
	return;
      }
      fMap[func]->addLocalConstraint(I,0);
      if (isa<SwitchInst>(I)) {
	//TODO later
      } else if (ReturnInst * ri = dyn_cast<ReturnInst>(I)) {
	//For getting the constraints on return values 
	LinearExpr *l1 = new LinearExpr(ri->getOperand(0),*Table);
	Constraint *c1 = new Constraint(var,l1,"=");
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
	if (ri->getNumOperands() > 0) {
	  getConstraints(ri->getOperand(0), rootp);
	}
	
      } else if (PHINode *p = dyn_cast<PHINode>(I)) {
	//its a normal PhiNode
	if (indMap.count(p) > 0) {
	  InductionVariable* iv = indMap[p];
	  if (iv->InductionType != InductionVariable::Unknown) {
	    //depending on the sign ,
	    // FIXME : for the following assumptions
	    // sign is assumed positive
	    ABCExprTree *abcetemp;
	    Value *v0 = p->getIncomingValue(0);
	    Value *v1 = p->getIncomingValue(1);
	  if (iv->Start != v0) { 
	    std::swap(v0, v1);
	  }
	  LinearExpr *l1 = new LinearExpr(v0,*Table);
	  Constraint *c1 = new Constraint(var,l1,">=");
	  Instruction *prevDef;
	  if ((prevDef = dyn_cast<Instruction>(v0))) {
	    getConstraints(prevDef,rootp);
	  };
	  abcetemp = new ABCExprTree(c1);
	  
	  //V1 is coming in from the loop 
	  l1 = new LinearExpr(v1,*Table);
	  c1 = new Constraint(var,l1,"<=");
	  if ((prevDef = dyn_cast<Instruction>(v1))) {
	    getConstraints(prevDef,rootp);
	  };
	  abcetemp = new ABCExprTree(abcetemp,new ABCExprTree(c1),"&&");  // 
	  *rootp = new ABCExprTree(*rootp,abcetemp,"&&");
	  } else  {
	    //Its OKay to conservatively ignore this phi node
	    // out << " In function " << I->getParent()->getParent()->getName() << "\n";
	    //	    out << " for the instruction " << I << "\n";
	    //	    out << " \n\n Induction Variable not Found \n\n ";
	    //	    out << "Array Bounds Check failed ";
	  }
	} else {
	  //	  out << " \n\n Induction Variable not Found \n\n ";
	  //	  out << "Array Bounds Check failed ";
	}
      } else if (isa<CallInst>(I)) {
	CallInst * CI = dyn_cast<CallInst>(I);
	//First we have to check if it is an RMalloc
	if (CI->getOperand(0)->getName() == REGION_MALLOC) {
	  //It is an RMalloc, we knoe it has only one argument 
	  Constraint *c = new Constraint(var, SimplifyExpression(I->getOperand(1),rootp),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	} else {
	  if (KnownFuncDB.find(CI->getOperand(0)->getName()) != KnownFuncDB.end()) {
	    //Its a known and trusted function
	    // we need to add the constraints carefully
	    addConstraintsForKnownFunctions(CI, rootp);
	  } else {
	  //we have to get constraints for arguments.
	    if (fMap.count(func) == 0) {
	      fMap[func] = new FuncLocalInfo();
	    }
	    getReturnValueConstraints(CI, rootp);
	    //getting the constraints on the arguments.
	    if (Function *Fn = dyn_cast<Function>(CI->getOperand(0))) {
	      Function::aiterator formalArgCurrent = Fn->abegin(), formalArgEnd = Fn->aend();
	      for (unsigned i = 1; formalArgCurrent != formalArgEnd; ++formalArgCurrent, ++i) {
		string varName = getValueName(formalArgCurrent);
		Value *OperandVal = CI->getOperand(i);
		LinearExpr *le = new LinearExpr(OperandVal,*Table);
		Constraint* c1 = new Constraint(varName,le,"=");
		ABCExprTree *temp = new ABCExprTree(c1);
		//		if (!isa<Constant>(OperandVal)) {
		  getConstraints(OperandVal,&temp);
		  //		}
		*rootp = new ABCExprTree(*rootp, temp, "&&");
	      }
	    }
	    LinearExpr *le1 = new LinearExpr(v,*Table);
	    Constraint *c1 = new Constraint(getValueName(I->getOperand(0)),le1,"=");
	    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
	  }
	}
      } else if (isa<AllocationInst>(I)) {
	//Note that this is for the local variables which are converted in to
	//allocas , we take care of the RMallocs in the CallInst case
      
	AllocationInst *AI = cast<AllocationInst>(I);
	if (const ArrayType *AT = dyn_cast<ArrayType>(AI->getType()->getElementType())) {
	  //sometime allocas have some array as their allocating constant !!
	  //We then have to generate constraints for all the dimensions
	  const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	  const ConstantSInt * signedOne = ConstantSInt::get(csiType,1);

	  Constraint *c = new Constraint(var, new LinearExpr(signedOne, *Table),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	  generateArrayTypeConstraints(var, AT, rootp);
	} else {
	  //This is the general case, where the allocas are allocated by some
	  //variable
	  Constraint *c = new Constraint(var, SimplifyExpression(I->getOperand(0),rootp),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	}
      } else if (isa<GetElementPtrInst>(I)) {
	

	GetElementPtrInst *GEP = cast<GetElementPtrInst>(I);
	Value *PointerOperand = I->getOperand(0);
	if (const PointerType *pType = dyn_cast<PointerType>(PointerOperand->getType()) ){
	  //this is for arrays inside structs 
	  if (const StructType *stype = dyn_cast<StructType>(pType->getElementType())) {
	    //getelementptr *key, long 0, ubyte 0, long 18
	    if (GEP->getNumOperands() == 4) {
	      if (const ArrayType *aType = dyn_cast<ArrayType>(stype->getContainedType(0))) {
		int elSize = aType->getNumElements();
		if (const ConstantSInt *CSI = dyn_cast<ConstantSInt>(I->getOperand(3))) {
		  elSize = elSize - CSI->getValue();
		  if (elSize == 0) {
		  //Dirty HACK, this doesnt work for more than 2 arrays in a struct!!
		    if (const ArrayType *aType2 = dyn_cast<ArrayType>(stype->getContainedType(1))) {
		      elSize = aType2->getNumElements();
		    }
		  }
		  const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
		  const ConstantSInt * signedOne = ConstantSInt::get(csiType,elSize);
		  Constraint *c = new Constraint(var, new LinearExpr(signedOne, *Table),"=");
		  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
		}
	      }
	    }
	  }
	}
	//dunno if this is a special case or need to be generalized
	//FIXME for now it is a special case.
	if (I->getNumOperands() == 2) {
	  getConstraints(PointerOperand,rootp);
	  getConstraints(GEP->getOperand(1),rootp);
	  LinearExpr *L1 = new LinearExpr(GEP->getOperand(1), *Table);
	  LinearExpr *L2 = new LinearExpr(PointerOperand, *Table);
	  L1->negate();
	  L1->addLinearExpr(L2);
	  Constraint *c = new Constraint(var, L1,"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	}
	//This is added for the special case found in the embedded bench marks
	//Normally GetElementPtrInst is taken care by the getSafetyConstraints
	//But sometimes you get a pointer to an array x = &x[0]
	//z = getelementptr x 0 0
	//getlelementptr z is equivalent to getelementptr x !
	if (I->getNumOperands() == 3) {
	  if (const PointerType *PT = dyn_cast<PointerType>(PointerOperand->getType())) {
	    if (const ArrayType *AT = dyn_cast<ArrayType>(PT->getElementType())) {
	      if (const ConstantSInt *CSI = dyn_cast<ConstantSInt>(I->getOperand(1))) {
		if (CSI->getValue() == 0) {
		  if (const ConstantSInt *CSI2 = dyn_cast<ConstantSInt>(I->getOperand(2))) {
		    if (CSI2->getValue() == 0) {
		      //Now add the constraint

		      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
		      const ConstantSInt * signedOne = ConstantSInt::get(csiType,AT->getNumElements());
		      Constraint *c = new Constraint(var, new LinearExpr(signedOne, *Table),"=");
		      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
		      
		    }
		  }
		}
	      }
	    }
	  }
	}
      } else {
	Constraint *c = new Constraint(var, SimplifyExpression(I,rootp),"=");
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
      }
      fMap[func]->addLocalConstraint(I,*rootp); //storing in the cache
    } else if (GlobalVariable *GV = dyn_cast<GlobalVariable>(v)) {
      //Its a global variable...
      //It could be an array
      var = getValueName(GV);
      if (const ArrayType *AT = dyn_cast<ArrayType>(GV->getType()->getElementType())) {
	const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	const ConstantSInt * signedOne = ConstantSInt::get(csiType,1);
	
	Constraint *c = new Constraint(var, new LinearExpr(signedOne, *Table),"=");
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	generateArrayTypeConstraintsGlobal(var, AT, rootp, 1);	  
      }
    }
  }

  void ArrayBoundsCheck::generateArrayTypeConstraintsGlobal(string var, const ArrayType *T, ABCExprTree **rootp, unsigned int numElem) {
    string var1 = var + "_i";
    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
    if (const ArrayType *AT = dyn_cast<ArrayType>(T->getElementType())) {
      const ConstantSInt * signedOne = ConstantSInt::get(csiType,1);
      Constraint *c = new Constraint(var1, new LinearExpr(signedOne, *Table),"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
      generateArrayTypeConstraintsGlobal(var1,AT, rootp, T->getNumElements() * numElem);
    } else {
      const ConstantSInt * signedOne = ConstantSInt::get(csiType,numElem * T->getNumElements());
      Constraint *c = new Constraint(var1, new LinearExpr(signedOne, *Table),"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
    }
  }
  
  
  void ArrayBoundsCheck::generateArrayTypeConstraints(string var, const ArrayType *T, ABCExprTree **rootp) {
    string var1 = var + "_i";
    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
    const ConstantSInt * signedOne = ConstantSInt::get(csiType,T->getNumElements());
    Constraint *c = new Constraint(var1, new LinearExpr(signedOne, *Table),"=");
    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
    if (const ArrayType *AT = dyn_cast<ArrayType>(T->getElementType())) {
      generateArrayTypeConstraints(var1,AT, rootp);
    }
  }
  
  ABCExprTree *ArrayBoundsCheck::getArgumentConstraints(Function & F) {
    //First check if it is in cache
    ABCExprTree *root = fMap[&F]->getArgumentConstraints();
    if (root) {
      return root;
    } else {
      root = 0;
      ABCExprTree *rootCallInst;
      //Its not there in cache, so we compute it
      DoneList.clear();
      DSGraph &G = budsPass->getDSGraph(F);
      const std::vector<DSCallSite> &callSites = G.getFunctionCalls();
      for (std::vector<DSCallSite>::const_iterator CSI = callSites.begin(),
	     CSE = callSites.end(); CSI != CSE; ++CSI) {
      //      for (Value::use_iterator I = F.use_begin(), E = F.use_end(); I != E; ++I) {
      //	User *U = *I;
	rootCallInst = 0;
	//	if (CallInst *CI = dyn_cast<CallInst>(U))  {
	if (CallInst *CI = dyn_cast<CallInst>(CSI->getCallSite().getInstruction())) {
	  //we need to AND the constraints on the arguments
	  Function::aiterator formalArgCurrent = F.abegin(), formalArgEnd = F.aend();
	  for (unsigned i = 1; formalArgCurrent != formalArgEnd; ++formalArgCurrent, ++i) {
	    if (i < CI->getNumOperands()) {
	      string varName = getValueName(formalArgCurrent);
	      Value *OperandVal = CI->getOperand(i);
	      LinearExpr *le = new LinearExpr(OperandVal,*Table);
	      Constraint* c1 = new Constraint(varName,le,"=");
	      ABCExprTree *temp = new ABCExprTree(c1);
	      if (!isa<Constant>(OperandVal)) {
		getConstraints(OperandVal,&temp);
	      }
	      if (!rootCallInst)  {
		rootCallInst = temp;
	      } else {
		rootCallInst = new ABCExprTree(rootCallInst, temp, "&&");
	      }
	    }
	  }
	  if (!root) {
	    root = rootCallInst;
	  } else if (rootCallInst) {
	    root = new ABCExprTree(root, rootCallInst, "||");
	  }
	}
      }
    }
    fMap[&F]->addArgumentConstraints(root);  //store it in cache
    return root;
  }


  void ArrayBoundsCheck::printStandardArguments(const Module *M, ostream & out) {
    for (Module::const_iterator fI = M->begin(), fE = M->end(); fI != fE; ++fI) {
      if (fI->getName() == "main") {
	Function::const_aiterator formalArgCurrent = fI->abegin(), formalArgEnd = fI->aend();
	if (formalArgCurrent != formalArgEnd) {
	  //relyingon front end's ability to get two arguments
	  string argcname = getValueName(formalArgCurrent);//->getName();
	  ++formalArgCurrent;
	  string argvname = getValueName(formalArgCurrent);//->getName();
	  out << " && " << argcname << " = " << argvname ;
	  break;
	}
      }
    }
  }

  void ArrayBoundsCheck::printSymbolicStandardArguments(const Module *M, ostream & out) {
    for (Module::const_iterator fI = M->begin(), fE = M->end(); fI != fE; ++fI) {
      if (fI->getName() == "main") {
	Function::const_aiterator formalArgCurrent = fI->abegin(), formalArgEnd = fI->aend();
	if (formalArgCurrent != formalArgEnd) {
	  //relyingon front end's ability to get two arguments
	  string argcname = getValueName(formalArgCurrent);//->getName();
	  ++formalArgCurrent;
	  string argvname = getValueName(formalArgCurrent);//->getName();
	  out << "symbolic " << argcname << ";\n";
	  out << "symbolic " << argvname  << ";\n";
	  break;
	}
      }
    }
  }

  
  //FIXME doesn't handle any kind of recursion 
  void ArrayBoundsCheck::checkSafety(Function &F) {
    //    out << "fn name " << F.getName() << "\n"; 
    provenSafe[&F] =  1;
    if (F.isExternal()) {
      //Crazy that this should appear here !!1
      if (KnownFuncDB.find(F.getName()) == KnownFuncDB.end()) {
	std::cerr << "unknown function \n" << F.getName() << "\n";
      }
      return;
    }
    //first check if all its parents are safe, i.e. all calling functions are safe
    DSGraph &G = budsPass->getDSGraph(F);
    const std::vector<DSCallSite> &callSites = G.getFunctionCalls();

    //    for (Value::use_iterator I = F.use_begin(), E = F.use_end(); I != E; ++I) {
    //      User *U = *I;
    for (std::vector<DSCallSite>::const_iterator CSI = callSites.begin(),
	   CSE = callSites.end(); CSI != CSE; ++CSI) {
      Function *parentfunc  = &(CSI->getCaller());
      //	  ->getParent()->getParent();
	//      out << " Inside Check Safety Uses paren func "  << getValueName(parentfunc) << " func " <<  getValueName(&F) << endl;
      if (!(provenSafe.count(parentfunc))) { //may be wa have to change it to indicate the end 
	checkSafety(*parentfunc);
      }
    }
    // we know that all its parents are safe.
    //   out << " Inside Check Safety " << getValueName(&F) << endl;
    if (fMap[&F] != 0) {
      MemAccessInstListType MemAccessInstList = fMap[&F]->getMemAccessInstList();
      MemAccessInstListIt maI = MemAccessInstList.begin(), maE = MemAccessInstList.end();
      for (; maI != maE; ++maI) {
	ABCExprTree *root = fMap[&F]->getSafetyConstraint(*maI);
	//       out << " Inside Check Safety getting root for " << getValueName(&F) << endl;
	ABCExprTree * argConstraints = getArgumentConstraints(F);
	if (argConstraints) {
	  root = new ABCExprTree(root,argConstraints,"&&");
	}
	//omega stuff should go in here.
	Omega(*maI,root);
      }
    }
  }

#define parentR p2cdes[0]  
#define childW p2cdes[1]  
#define childR c2pdes[0]  
#define parentW c2pdes[1]
  
void ArrayBoundsCheck::Omega(Instruction *maI, ABCExprTree *root ) {
  int p2cdes[2];
  int c2pdes[2];
  pid_t pid;
  pipe(p2cdes);
  pipe(c2pdes);

  
  if ((pid = fork())) {
    //this is the parent
    close(childR);
    close(childW);
    fcntl(parentW, F_SETFL, O_NONBLOCK);
    boost::fdostream out(parentW);
    const Module *M = (maI)->getParent()->getParent()->getParent();
    if (root != 0) {
      root->printOmegaSymbols(out);
      //      root->printOmegaSymbols(std::cerr);
    }
    printSymbolicStandardArguments(M, out);
    //    out << "<<" << OMEGA_TMP_INCLUDE_FILE << ">>\n";
    out << " P" <<countA << " := {[i] : \n";
    if (root != 0)root->print(out);
    printStandardArguments(M, out);
    //    out << " && (argv = argc) ";
    out << "};\n Hull P"<<countA++ << ";\n" ;
    close(parentW);
    int perl2parent[2];
    pipe(perl2parent);
    if (!fork()){
      //child
      close(perl2parent[0]);
      close(fileno(stdout));
      dup(perl2parent[1]);
      close(fileno(stdin));
      dup(parentR);
       if (execve("/home/vadve/dhurjati/bin/omega.pl",NULL,NULL) == -1) {
	perror("execve error \n");
	exit(-1);
      }
    } else {
      int result;
      close(perl2parent[1]);
      boost::fdistream inp(perl2parent[0]);
      std::cerr << "waiting for output " << countA << "\n";
      inp >> result;
      close(perl2parent[0]);
      //      read(perl2parent[0],&result,4);
      if (result == 1) {
	std::cerr << "proved safe \n";
	//Omega proved SAFE 
      } else {
	std::cerr << "cannot prove safe " << countA;
	UnsafeGetElemPtrs.push_back(maI);
      }
    }
  } else if (pid < 0) {
    perror("fork error \n");
    exit(-1);
  } else {
    //pid == 0
    // this is child
    close(parentW);
    close(parentR);
    close(fileno(stdin));
    dup(childR);
    close(fileno(stdout));
    dup(childW);
    if (execve("/home/vadve/dhurjati/bin/oc",NULL,NULL) == -1) {
      perror("execve error \n");
      exit(-1);
    }
  }
}
  bool ArrayBoundsCheck::run(Module &M) {
    budsPass = &getAnalysis<CompleteBUDataStructures>();
    Table = new SlotCalculator(&M, true);

    initialize(M);
    /* printing preliminaries */
    outputDeclsForOmega(M);
    includeOut.close();
    //  out << "outputting decls for Omega done" <<endl;


    //  out << " First Collect Safety Constraints ";
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
      Function &F = *I;
      if (!(F.hasName()) || (KnownFuncDB.find(F.getName()) == KnownFuncDB.end()))
	collectSafetyConstraints(F);
    }

    //  out << "Output of collect safety constraints  \n";
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
      //    out << " Function Name "<< getValueName(I) << "\n";
      if (fMap[I] != 0) {
	// we dont have info for external functions 
	MemAccessInstListType MemAccessInstList = fMap[I]->getMemAccessInstList();
	MemAccessInstListIt maI = MemAccessInstList.begin(), maE = MemAccessInstList.end();
	for (; maI != maE; ++maI) {
	  ABCExprTree *root = fMap[I]->getSafetyConstraint(*maI);
	  //	root->print(out);
	}
      }
    }

    //  out << " Now checking the constraints  \n ";
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
      if (!(provenSafe.count(I) != 0)) checkSafety(*I);
    }
    //    out << " NumAccessses" <<count << " := {[i] : i = " <<count<< " };\n";
    //  out << "done \n" ;
    return false;
  }


  void ArrayBoundsCheck::collectSafetyConstraints(Function &F) {
    if (fMap.count(&F) == 0) {
      fMap[&F] = new FuncLocalInfo();
    }
      
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      Instruction *iLocal = *I;
      if (isa<CastInst>(iLocal)) {
	//This is an ugly hack for kill code to work
	if (isa<GetElementPtrInst>(iLocal->getOperand(0))) {
	  iLocal = cast<Instruction>(iLocal->getOperand(0));
	}
      }
      if (isa<GetElementPtrInst>(iLocal)) {
	GetElementPtrInst *MAI = cast<GetElementPtrInst>(iLocal);
	if (const PointerType *PT = dyn_cast<PointerType>(MAI->getPointerOperand()->getType())) {
	  if (!isa<StructType>(PT->getElementType())) {
	    User::op_iterator mI = MAI->op_begin(), mE = MAI->op_end();
	    if (mI == mE) {
	      continue;
	    }
	    mI++;
	    ABCExprTree *root;
	    string varName = getValueName(MAI->getPointerOperand());
	    if ((*mI)->getType() == Type::LongTy) {
	      LinearExpr *le = new LinearExpr(*mI,*Table);
	      Constraint* c1 = new Constraint(varName,le,"<="); // length < index
	      ABCExprTree* abctemp1 = new ABCExprTree(c1);
	      Constraint* c2 = new Constraint("0",le,">",true); // 0 > index
	      ABCExprTree* abctemp2 = new ABCExprTree(c2);
	      root = new ABCExprTree(abctemp1, abctemp2, "||");
	    }
	    mI++;
	    for (; mI != mE; ++mI) {
	      if ((*mI)->getType() == Type::LongTy) {
		LinearExpr *le = new LinearExpr(*mI,*Table);
		varName = varName+"_i" ;
		Constraint* c1 = new Constraint(varName,le,"<="); // length < index
		ABCExprTree* abctemp1 = new ABCExprTree(c1);
		Constraint* c2 = new Constraint("0",le,">",true); // 0 > index
		ABCExprTree* abctemp2 = new ABCExprTree(c2);
		ABCExprTree*abctempor = new ABCExprTree(abctemp1,abctemp2,"||"); // abctemp1 || abctemp2
		root = new ABCExprTree(root, abctempor, "||");
	      }
	    }
	    //reinitialize mI , now getting the constraints on the indices
	    //We need to clear DoneList since we are getting constraints for a new access.
	    DoneList.clear();
	    addControlDependentConditions(MAI->getParent(), &root);
	    mI = MAI->idx_begin();
	    for (; mI != mE; ++mI) {
	      if ((*mI)->getType() == Type::LongTy) {
		getConstraints(*mI,&root);
	      }
	    }
	    getConstraints(MAI->getPointerOperand(),&root);
	    fMap[&F]->addSafetyConstraint(MAI,root);
	    fMap[&F]->addMemAccessInst(MAI);
	  }
	}
      }
    }
  }


  void ArrayBoundsCheck::addBranchConstraints(BranchInst *BI,BasicBlock *Successor, ABCExprTree **rootp) {
    //this has to be a conditional branch, otherwise we wouldnt have been here
    assert((BI->isConditional()) && "abcd wrong pi node");
    if (SetCondInst *SCI = dyn_cast<SetCondInst>(BI->getCondition())) {

      //SCI now has the conditional statement
      Value *operand0 = SCI->getOperand(0);
      Value *operand1 = SCI->getOperand(1);

      getConstraints(operand0,rootp);
      getConstraints(operand1,rootp);
      

      LinearExpr *l1 = new LinearExpr(operand1,*Table);

      string var0 = getValueName(operand0);
      Constraint *ct;

      switch(SCI->getOpcode()) {
      case Instruction::SetLE : 
	//there are 2 cases for each opcode!
	//its the true branch or the false branch
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,"<=");
	} else {
	  ct = new Constraint(var0,l1,">");
	}
	break;
      case Instruction::SetNE : 
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,"!=");
	} else {
	  ct = new Constraint(var0,l1,"=");
	}
	break;
      case Instruction::SetEQ : 
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,"=");
	} else {
	  //false branch
	  ct = new Constraint(var0,l1,"!=");
	}
	break;
      case Instruction::SetGE : 
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,">=");
	} else {
	  //false branch
	  ct = new Constraint(var0,l1,"<");
	}
	break;
      case Instruction::SetLT : 
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,"<");
	} else {
	  //false branch
	  ct = new Constraint(var0,l1,">=");
	}
	break;
      case Instruction::SetGT :
	if (BI->getSuccessor(0) == Successor) {
	  //true branch 
	  ct = new Constraint(var0,l1,">");
	} else {
	  //false branch
	  ct = new Constraint(var0,l1,"<=");
	}
	break;
      default :
	break;
      }
    *rootp = new ABCExprTree(*rootp,new ABCExprTree(ct),"&&");  // 
    }
  }



  // SimplifyExpression: Analyze an expression inorder to simplify it


  LinearExpr* ArrayBoundsCheck::SimplifyExpression( Value *Expr, ABCExprTree **rootp) {
    assert(Expr != 0 && "Can't classify a null expression!");
    if (Expr->getType() == Type::FloatTy || Expr->getType() == Type::DoubleTy)
      return new LinearExpr(Expr, *Table) ;   // nothing  known return variable itself

    switch (Expr->getValueType()) {
    case Value::InstructionVal: break;    // Instruction... hmmm... investigate.
    case Value::TypeVal:   case Value::BasicBlockVal:
    case Value::FunctionVal:
      assert(1 && "Unexpected expression type to classify!");
      //      out << "Bizarre thing to expr classify: " << Expr << "\n";
      return new LinearExpr(Expr, *Table);
    case Value::GlobalVariableVal:        // Global Variable & Method argument:
    case Value::ArgumentVal:        // nothing known, return variable itself
      return new LinearExpr(Expr, *Table);
    case Value::ConstantVal:              // Constant value, just return constant
      Constant *CPV = cast<Constant>(Expr);
      if (CPV->getType()->isIntegral()) { // It's an integral constant!
	if (ConstantArray *CA = dyn_cast<ConstantArray>(CPV)) {
	  const std::vector<Use> &Values = CA->getValues();
	  ConstantInt *CPI = cast<ConstantInt>(Values[1]);
	  return new LinearExpr(CPI, *Table);
	} else if (ConstantInt *CPI = dyn_cast<ConstantInt>(Expr)) {
	  return new LinearExpr(CPI, *Table);
	}
      }
      return new LinearExpr(Expr, *Table); //nothing known, just return itself
    }
  
    Instruction *I = cast<Instruction>(Expr);
    const Type *Ty = I->getType();

    switch (I->getOpcode()) {       // Handle each instruction type seperately
    case Instruction::Add: {
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0), rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), *Table);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1), rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), *Table);
      }
      Left->addLinearExpr(Right);
      return Left;
    }
    case Instruction::Sub: {
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0), rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), *Table);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1), rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), *Table);
      }
      Right->negate();
      Left->addLinearExpr(Right);
      return Left;
    }
    case Instruction::SetLE : 
    case Instruction::SetNE : 
    case Instruction::SetEQ : 
    case Instruction::SetGE : 
    case Instruction::SetLT : 
    case Instruction::SetGT : {
      LinearExpr* L = new LinearExpr(I->getOperand(1),*Table);
      return L;
    };
    case Instruction::Mul :
    
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0),rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), *Table);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1),rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), *Table);
      }
      return Left->mulLinearExpr(Right);
    }  // end switch
    if (isa<CastInst>(I)) {
      //    out << "dealing with cast instruction ";
      //FIXME .. this should be for all types not just sbyte 
      const Type *opType = I->getOperand(0)->getType();
      if ((opType->isPrimitiveType()) && ((opType->getPrimitiveID() == Type::SByteTyID) || (opType->getPrimitiveID() == Type::UByteTyID))) {
	string number = "255";
	string number2 = "0";
	string var = getValueName(I);
	LinearExpr *l1 = new LinearExpr(I,*Table);
	Constraint *c1 = new Constraint(number,l1,">=",true);
	Constraint *c2 = new Constraint(number2,l1,"<=",true);
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c2),"&&");
	Constraint *c = new Constraint(var, SimplifyExpression(I->getOperand(0),rootp),"=");
	*rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	return l1;
      } else {
	//special case for casts to beginning of structs
	// the case is (sbyte*) (Struct with the first element arrauy)
	//If it is a cast from struct to something else * ...
	if (const PointerType *pType = dyn_cast<PointerType>(I->getType())){
	  const Type *eltype = pType->getElementType();
	  if (eltype->isPrimitiveType() && (eltype->getPrimitiveID() == Type::SByteTyID)) {
	    if (const PointerType *opPType = dyn_cast<PointerType>(opType)){
	      const Type *opEltype = opPType->getElementType();
	      if (const StructType *stype = dyn_cast<StructType>(opEltype)) {
		if (const ArrayType *aType = dyn_cast<ArrayType>(stype->getContainedType(0))) {
		  if (aType->getElementType()->isPrimitiveType()) {
		    int elSize = aType->getNumElements();
		    switch (aType->getElementType()->getPrimitiveID()) {
		    case Type::ShortTyID :
		    case Type::UShortTyID :  elSize *= 2; break;
		    case Type::IntTyID :
		    case Type::UIntTyID :  elSize *= 4; break;
		    case Type::LongTyID :
		    case Type::ULongTyID :  elSize *= 8; break;
		    default : break;
		    }
		    string varName = getValueName(I);
		    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
		    const ConstantSInt * signedOne = ConstantSInt::get(csiType,elSize);
		    LinearExpr *l1 = new LinearExpr(signedOne, *Table);
		    return l1;
		    //		    Constraint *c = new Constraint(varName, new LinearExpr(signedOne, *Table),"=");
		    //		    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
		  }
		}
	      }
	    }
	  }
	}
      }
      return SimplifyExpression(I->getOperand(0),rootp);
    } else  {
      getConstraints(I,rootp);
      LinearExpr* ret = new LinearExpr(I,*Table);
      return ret;
    }
    // Otherwise, I don't know anything about this value!
    return 0;
  }


Pass *createArrayBoundsCheckPass() { return new ABC::ArrayBoundsCheck(); }
