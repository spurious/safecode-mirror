//===- ArrayBoundCheck.cpp - ArrayBounds Checking (Omega)----------------===//
//
// Now we use the control dependence, post dominance frontier to generate constraints
// for 
//===----------------------------------------------------------------------===//
#include <unistd.h>
#include "utils/fdstream.h"
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/BasicBlock.h"
#include "ArrayBoundsCheck.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "omega.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/DataStructure/DSGraph.h"
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <fcntl.h>

#define NO_STATIC_CHECK
#define OMEGA_TMP_INCLUDE_FILE "omega_include.ip"

using namespace llvm;
using namespace ABC;
std::ostream &Out = std::cerr;
std::ofstream includeOut(OMEGA_TMP_INCLUDE_FILE);

//The following are filled from the preprocess pass, since they require
//fn passes
extern IndVarMap indMap; 
extern DominatorSet::DomSetMapType dsmt;
extern PostDominatorSet::DomSetMapType pdsmt;
extern PostDominanceFrontier::DomSetMapType pdfmt;

static bool dominates(BasicBlock *bb1, BasicBlock *bb2) {
  DominatorSet::DomSetMapType::const_iterator dsmtI = dsmt.find(bb1);
  assert((dsmtI != dsmt.end()) && " basic block not found in dominator set");
  return (dsmtI->second.count(bb2) != 0);
}

static bool postDominates(BasicBlock *bb1, BasicBlock *bb2) {
  PostDominatorSet::DomSetMapType::const_iterator pdsmtI = pdsmt.find(bb1);
  if (pdsmtI == pdsmt.end())
    return false;
  return (pdsmtI->second.count(bb2) != 0);
}

//count the number of problems given to Omega
static  unsigned countA = 0;
//This will tell us whether the collection of constraints
//depends on the incoming args or not
//Do we need this to be global?
static bool reqArgs = false;
//a hack for llvm's malloc instruction which concerts all ints to uints
//This is not really necessary, as it is checked in the pool allocation run-time
//library 
static bool fromMalloc = false;

//Interprocedural ArrayBoundsCheck pass
RegisterOpt<ArrayBoundsCheck> abc1("abc1","Array Bounds Checking pass");


 void ArrayBoundsCheck::initialize(Module &M) {
    KnownFuncDB.insert("snprintf"); //added the format string & string check
    KnownFuncDB.insert("strcpy"); //need to add the extra checks 
    KnownFuncDB.insert("memcpy"); //need to add the extra checks 
    KnownFuncDB.insert("llvm.memcpy"); //need to add the extra checks 
    KnownFuncDB.insert("strlen"); //Gives return value constraints 
    KnownFuncDB.insert("read"); // read requires checks and return value constraints
    KnownFuncDB.insert("fread"); //need to add the extra checks 

    KnownFuncDB.insert("fprintf"); //need to check if it is not format string
    KnownFuncDB.insert("printf"); //need to check if it is not format string 
    KnownFuncDB.insert("vfprintf"); //need to check if it is not format string 
    KnownFuncDB.insert("syslog"); //need to check if it is not format string 

    KnownFuncDB.insert("memset"); //need to check if we are not setting outside
    KnownFuncDB.insert("llvm.memset"); //need to check if we are not setting outside
    KnownFuncDB.insert("gets"); // need to check if the char array is greater than 80
    KnownFuncDB.insert("strchr"); //FIXME check has not been added yet 
    KnownFuncDB.insert("sprintf"); //FIXME to add extra checks
    KnownFuncDB.insert("fscanf"); //Not sure if it requires a check

    //Not sure if the following require any checks. 
    KnownFuncDB.insert("llvm.va_start");
    KnownFuncDB.insert("llvm.va_end");
    
    //The following doesnt require checks
    KnownFuncDB.insert("random");
    KnownFuncDB.insert("rand");
    KnownFuncDB.insert("clock");
    KnownFuncDB.insert("exp");
    KnownFuncDB.insert("fork");
    KnownFuncDB.insert("wait");
    KnownFuncDB.insert("fflush");
    KnownFuncDB.insert("fclose");
    KnownFuncDB.insert("alarm");
    KnownFuncDB.insert("signal");
    KnownFuncDB.insert("setuid");
    KnownFuncDB.insert("__errno_location");
    KnownFuncDB.insert("log");
    KnownFuncDB.insert("srand48");
    KnownFuncDB.insert("drand48");
    KnownFuncDB.insert("lrand48");
    KnownFuncDB.insert("times"); 
    KnownFuncDB.insert("puts");
    KnownFuncDB.insert("putchar");
    KnownFuncDB.insert("strcmp");
    KnownFuncDB.insert("strtol");
    KnownFuncDB.insert("fopen");
    KnownFuncDB.insert("fwrite");
    KnownFuncDB.insert("fgetc");
    KnownFuncDB.insert("getc");
    KnownFuncDB.insert("open");
    KnownFuncDB.insert("feof");
    KnownFuncDB.insert("fputc");
    KnownFuncDB.insert("atol");
    KnownFuncDB.insert("atoi");
    KnownFuncDB.insert("atof");
    KnownFuncDB.insert("exit");
    KnownFuncDB.insert("perror");
    KnownFuncDB.insert("sqrt");
    KnownFuncDB.insert("floor");
    KnownFuncDB.insert("pow");
    KnownFuncDB.insert("abort");
    KnownFuncDB.insert("srand");
    KnownFuncDB.insert("perror");
    KnownFuncDB.insert("__isnan");
    KnownFuncDB.insert("__main");
    KnownFuncDB.insert("ceil");
  }
  
  void ArrayBoundsCheck::outputDeclsForOmega(Module& M) {
    for (Module::iterator FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
      Function *F = FI;
      includeOut << "symbolic   Unknown;\n"
                 << "symbolic   argc;\n"
                 << "symbolic   argv;\n"
                 << "symbolic " << getValueName(F) <<"; \n";

      for (Function::ArgumentListType::iterator aI=F->getArgumentList().begin(),
           aE = F->getArgumentList().end(); aI != aE; ++aI) {
        includeOut << "symbolic   "
                   << getValueName((aI))
                   << ";\n";
      }

      for (Module::global_iterator gI = M.global_begin(), gE = M.global_end();
           gI != gE; ++gI) {
        includeOut << "symbolic   "
                   << getValueName((gI))
                   << ";\n";
        if (const ArrayType *AT = dyn_cast<ArrayType>(gI->getType()->getElementType())) {
          printarraytype(getValueName(gI), AT);
        }
      }

      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        if ((&*I)->getType() != Type::VoidTy) {
          includeOut << "symbolic   "
                     << getValueName(&*I)
                     << ";\n";

          if (AllocationInst *AI = dyn_cast<AllocationInst>(&*I)) {
            // We have to see the dimension of the array that this alloca is
            // pointing to
            // If the allocation is done by constant, then its a constant array
            // else its a normal alloca which we already have taken care of  
            if (const ArrayType *AT = dyn_cast<ArrayType>(AI->getType()->getElementType())) {
              printarraytype(getValueName(&*I), AT);
            }
          }
        }
      }
    }
  }

  string ArrayBoundsCheck::getValueName(const Value *V) {
    return Mang->getValueName(V);
  }

  void ArrayBoundsCheck::printarraytype(string var,const ArrayType  *T) {
    string var1 = var + "_i";
    includeOut << "symbolic   " << var1 << ";\n";
    if (const ArrayType *AT = dyn_cast<ArrayType>(T->getElementType())) {
      printarraytype(var1,AT);
    }
  }

ABCExprTree* ArrayBoundsCheck::getReturnValueConstraints(Function *f) {
  bool localSave = reqArgs;
  const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
  const ConstantSInt * signedzero = ConstantSInt::get(csiType,0);
  string var = "0";
  Constraint *c = new Constraint(var, new LinearExpr(signedzero, Mang),"=");
  ABCExprTree *root = new ABCExprTree(c); //dummy constraint 
  Function::iterator bI = f->begin(), bE = f->end();
  for (;bI != bE; ++bI) {
    BasicBlock *bb = bI;
    if (ReturnInst *RI = dyn_cast<ReturnInst>(bb->getTerminator()))  
      getConstraints(RI,&root);
  }
  reqArgs = localSave ; //restore to the original
  return root;
}

void ArrayBoundsCheck::addFormalToActual(Function *Fn, CallInst *CI, ABCExprTree **rootp) {
  LinearExpr *le1 = new LinearExpr(CI,Mang);
  Constraint *c1 = new Constraint(getValueName(Fn),le1,"=");
  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
  
  Function::arg_iterator formalArgCurrent = Fn->arg_begin(),
                         formalArgEnd     = Fn->arg_end();
  for (unsigned i = 1;
       formalArgCurrent != formalArgEnd;
       ++formalArgCurrent, ++i) {
    string varName = getValueName(formalArgCurrent);
    Value *OperandVal = CI->getOperand(i);
    LinearExpr *le = new LinearExpr(OperandVal,Mang);
    Constraint* c1 = new Constraint(varName,le,"=");
    ABCExprTree *temp = new ABCExprTree(c1);
    *rootp = new ABCExprTree(*rootp, temp, "&&"); //and of all arguments
  }
}

// This is an auxillary function used by getConstraints
// gets the constraints on the return value interms of its arguments
// and ands it with the existing rootp!
void ArrayBoundsCheck::getConstraintsAtCallSite(CallInst *CI,ABCExprTree **rootp) {
  if (Function *pf = dyn_cast<Function>(CI->getOperand(0))) {
    if (pf->isExternal()) {
      *rootp = new ABCExprTree(*rootp,addConstraintsForKnownFunctions(pf, CI), "&&");
      addFormalToActual(pf, CI, rootp);
    } else {
      if (buCG->isInSCC(pf)) {
        std::cerr << "Ignoring return values on function in recursion\n";
        return; 
      }
      *rootp = new ABCExprTree(*rootp,getReturnValueConstraints(pf), "&&");
      addFormalToActual(pf, CI, rootp);
    }
    //Now get the constraints on the actual arguemnts for the original call site 
    for (unsigned i =1; i < CI->getNumOperands(); ++i) 
      getConstraints(CI->getOperand(i),rootp);
  } else {
    //Indirect Calls
    ABCExprTree *temproot = 0;
    // Loop over all of the actually called functions...
    CompleteBUDataStructures::callee_iterator I = cbudsPass->callee_begin(CI),
                                              E = cbudsPass->callee_end(CI);
    //    assert((I != E) && "Indirect Call site doesn't have targets ???? ");
    //Actually thats fine, we ignore the return value constraints ;)
    for(; I != E; ++I) {
      CallSite CS = CallSite::get(I->first);
      if ((I->second->isExternal()) ||
          (KnownFuncDB.find(I->second->getName()) != KnownFuncDB.end()) ) {
        ABCExprTree * temp = addConstraintsForKnownFunctions(I->second, CI);
        addFormalToActual(I->second, CI, &temp);
        if (temproot) {
          // We need to or them 
          temproot = new ABCExprTree(temproot, temp, "||");
        } else {
          temproot = temp;
        }
      } else {
        if (buCG->isInSCC(I->second)) {
          std::cerr << "Ignoring return values on function in recursion\n";
          return;
        }
        ABCExprTree * temp = getReturnValueConstraints(I->second);
        addFormalToActual(I->second, CI, &temp);
        if (temproot) {
          temproot = new ABCExprTree(temproot, temp, "||");
        } else {
          temproot = temp;
        }
      }
    }
    if (temproot) {
      *rootp = new ABCExprTree(*rootp, temproot, "&&");
      // Now get the constraints on the actual arguemnts for the original
      // call site 
      for (unsigned i =1; i < CI->getNumOperands(); ++i) {
        getConstraints(CI->getOperand(i),rootp);
      }
    }
  }
}

  void ArrayBoundsCheck::addControlDependentConditions(BasicBlock *currentBlock, ABCExprTree **rootp) {
    PostDominanceFrontier::const_iterator it = pdfmt.find(currentBlock);
    if (it != pdfmt.end()) {
      const PostDominanceFrontier::DomSetType &S = it->second;
      if (S.size() > 0) {
        PostDominanceFrontier::DomSetType::iterator pCurrent = S.begin(),
                                                    pEnd     = S.end();
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
            if (dominates(*pCurrent, currentBlock)) {
              dominated = true;
              rdominated = false;
              continue;
            }
          }
          if (dominates(currentBlock, *pCurrent)) {
            rdominated = rdominated & true;
            continue;
          } else {
#if 0
            out << "In function " << currentBlock->getParent()->getName();
            out << "for basic block " << currentBlock->getName();
            out << "Something wrong .. non affine or unstructured control flow ??\n";
#endif
            dominated = false;
            break;
          }
        }
        if ((dominated) || (rdominated)) {
          // Now we are sure that the control dominance is proper
          // i.e. it doesn't have unstructured control flow 
          
          PostDominanceFrontier::DomSetType::iterator pdCurrent = S.begin(),
                                                      pdEnd     = S.end();
          for (; pdCurrent != pdEnd; ++pdCurrent) {
            BasicBlock *CBB = *pdCurrent;
            if (DoneList.find(CBB) == DoneList.end()) {
              TerminatorInst *TI = CBB->getTerminator();
              if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
                for (unsigned index = 0; index < BI->getNumSuccessors(); ++index) {
                  BasicBlock * succBlock = BI->getSuccessor(index);
                  if (postDominates(currentBlock, succBlock)) {
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

  // adds constraints for known functions 
  ABCExprTree* ArrayBoundsCheck::addConstraintsForKnownFunctions(Function *kf, CallInst *CI) {
    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
    const ConstantSInt * signedzero = ConstantSInt::get(csiType,0);
    string var = "0";
    Constraint *c = new Constraint(var, new LinearExpr(signedzero, Mang),"=");
    ABCExprTree *root = new ABCExprTree(c); //dummy constraint 
    ABCExprTree **rootp = &root;
    string funcName = kf->getName();
    if (funcName == "memcpy") {
      string var = getValueName(CI->getOperand(1));
      LinearExpr *l1 = new LinearExpr(CI->getOperand(2),Mang);
      Constraint *c1 = new Constraint(var,l1,">=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"||");
      getConstraints(CI->getOperand(1), rootp);
      getConstraints(CI->getOperand(2), rootp);
    } else if (funcName == "llvm.memcpy") {
      string var = getValueName(CI->getOperand(1));
      LinearExpr *l1 = new LinearExpr(CI->getOperand(2),Mang);
      Constraint *c1 = new Constraint(var,l1,">=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"||");
      getConstraints(CI->getOperand(1), rootp);
      getConstraints(CI->getOperand(2), rootp);
    } else if (funcName == "strlen") {
      string var = getValueName(CI);
      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
      const ConstantSInt * signedzero = ConstantSInt::get(csiType,0);
      
      Constraint *c = new Constraint(var, new LinearExpr(signedzero, Mang),">=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
      LinearExpr *l1 = new LinearExpr(CI->getOperand(1),Mang);
      Constraint *c1 = new Constraint(var,l1,"<");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(1), rootp);
    } else if (funcName == "read") {
      string var = getValueName(CI);
      LinearExpr *l1 = new LinearExpr(CI->getOperand(3),Mang);
      Constraint *c1 = new Constraint(var,l1,"<=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(3), rootp);
      
    } else if (funcName == "fread") {
      string var = getValueName(CI);
      LinearExpr *l1 = new LinearExpr(CI->getOperand(2),Mang);
      LinearExpr *l2 = new LinearExpr(CI->getOperand(3),Mang);
      l2->mulLinearExpr(l1);
      Constraint *c1 = new Constraint(var,l2,"<=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
      getConstraints(CI->getOperand(3), rootp);
      getConstraints(CI->getOperand(2), rootp);
      
    } else {
      //      out << funcName << " is not supported yet \n";
      // Ignoring some functions is okay as long as they are not part of the
      //one of the multiple indirect calls
      assert((CI->getOperand(0) == kf) && "Need to handle this properly \n");
    }
    return root;
  }


  void ArrayBoundsCheck::getConstraints(Value *v, ABCExprTree **rootp) {
    string tempName1 = getValueName(v);
    LinearExpr *letemp1 = new LinearExpr(v,Mang);
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
      if  (fMap.count(func)) {
	if (fMap[func]->inLocalConstraints(I)) { //checking the cache
	  if (fMap[func]->getLocalConstraint(I) != 0) {
	    *rootp = new ABCExprTree(*rootp, fMap[func]->getLocalConstraint(I),"&&");
	  }
	  return;
	}
      } else {
	fMap[func] = new FuncLocalInfo();
      }
      fMap[func]->addLocalConstraint(I,0);
      if (isa<SwitchInst>(I)) {
	//TODO later
      } else if (ReturnInst * ri = dyn_cast<ReturnInst>(I)) {
	if (ri->getNumOperands() > 0) {
	//For getting the constraints on return values 
	  LinearExpr *l1 = new LinearExpr(ri->getOperand(0),Mang);
	  Constraint *c1 = new Constraint(var,l1,"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
	  getConstraints(ri->getOperand(0), rootp);
	}
	
      } else if (PHINode *p = dyn_cast<PHINode>(I)) {
	//its a normal PhiNode
	if (indMap.count(p) > 0) {
	  //We know that this is the canonical induction variable
	  //First get the upper bound
	  Value *UBound = indMap[p];
	  LinearExpr *l1 = new LinearExpr(UBound, Mang);
	  Constraint *c1 = new Constraint(var, l1, "<");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");

	  const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	  const ConstantSInt * signedzero = ConstantSInt::get(csiType,0);
	  LinearExpr *l2 = new LinearExpr(signedzero, Mang);
	  Constraint *c2 = new Constraint(var, l2, ">=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c2),"&&");
	  
	  getConstraints(UBound, rootp);
	  
	}
      } else if (isa<CallInst>(I)) {
	CallInst * CI = dyn_cast<CallInst>(I);
	//First we have to check if it is an RMalloc
	if (CI->getOperand(0)->getName() == "RMalloc") {
	  //It is an RMalloc, we knoe it has only one argument 
	  Constraint *c = new Constraint(var, SimplifyExpression(I->getOperand(1),rootp),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	} else {
	  if (fMap.count(func) == 0) {
	    fMap[func] = new FuncLocalInfo();
	  }
	  //This also get constraints for arguments of CI
	  getConstraintsAtCallSite(CI, rootp);
	}
      }
      else if (isa<AllocationInst>(I)) {
	//Note that this is for the local variables which are converted in to
	//allocas, mallocs , we take care of the RMallocs (CASES work)in the CallInst case
	AllocationInst *AI = cast<AllocationInst>(I);
	if (const ArrayType *AT = dyn_cast<ArrayType>(AI->getType()->getElementType())) {
	  //sometime allocas have some array as their allocating constant !!
	  //We then have to generate constraints for all the dimensions
	  const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
	  const ConstantSInt * signedOne = ConstantSInt::get(csiType,1);

	  Constraint *c = new Constraint(var, new LinearExpr(signedOne, Mang),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	  generateArrayTypeConstraints(var, AT, rootp);
	} else {
	  //This is the general case, where the allocas/mallocs are allocated by some
	  //variable
	  //Ugly hack because of the llvm front end's cast of
	  //argument of malloc to uint
	  fromMalloc = true;
	  Value *sizeVal = I->getOperand(0) ;
	  //	  if (CastInst *csI = dyn_cast<CastInst>(I->getOperand(0))) {
	  //	    const Type *toType = csI->getType();
	  //	    const Type *fromType = csI->getOperand(0)->getType();
	  //	    if ((toType->isPrimitiveType()) && (toType->getPrimitiveID() == Type::UIntTyID)) {
	  //	      sizeVal = csI->getOperand(0);
	  //	  }
	  //	  }
	  Constraint *c = new Constraint(var, SimplifyExpression(sizeVal,rootp),"=");
	  fromMalloc = false;
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
		  Constraint *c = new Constraint(var, new LinearExpr(signedOne, Mang),"=");
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
	  LinearExpr *L1 = new LinearExpr(GEP->getOperand(1), Mang);
	  LinearExpr *L2 = new LinearExpr(PointerOperand, Mang);
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
		      Constraint *c = new Constraint(var, new LinearExpr(signedOne, Mang),"=");
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
	
	Constraint *c = new Constraint(var, new LinearExpr(signedOne, Mang),"=");
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
      Constraint *c = new Constraint(var1, new LinearExpr(signedOne, Mang),"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
      generateArrayTypeConstraintsGlobal(var1,AT, rootp, T->getNumElements() * numElem);
    } else {
      const ConstantSInt * signedOne = ConstantSInt::get(csiType,numElem * T->getNumElements());
      Constraint *c = new Constraint(var1, new LinearExpr(signedOne, Mang),"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
    }
  }
  
  
  void ArrayBoundsCheck::generateArrayTypeConstraints(string var, const ArrayType *T, ABCExprTree **rootp) {
    string var1 = var + "_i";
    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
    const ConstantSInt * signedOne = ConstantSInt::get(csiType,T->getNumElements());
    Constraint *c = new Constraint(var1, new LinearExpr(signedOne, Mang),"=");
    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
    if (const ArrayType *AT = dyn_cast<ArrayType>(T->getElementType())) {
      generateArrayTypeConstraints(var1,AT, rootp);
    } else if (const StructType *ST = dyn_cast<StructType>(T->getElementType())) {
      //This will only work one level of arrays and structs
      //If there are arrays inside a struct then this will
      //not help us prove the safety of the access ....
      unsigned Size = getAnalysis<TargetData>().getTypeSize(ST);
      string var2 = var1 + "_i";
      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
      const ConstantSInt * signedOne = ConstantSInt::get(csiType,Size);
      Constraint *c = new Constraint(var2, new LinearExpr(signedOne, Mang),"=");
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
    }
  }
  
ABCExprTree *ArrayBoundsCheck::getArgumentConstraints(Function & F) {
    if (buCG->isInSCC(&F)) return 0; //Ignore recursion for now
    std::set<Function *> reqArgFList;
    bool localSave = reqArgs;
   //First check if it is in cache
    ABCExprTree *root = fMap[&F]->getArgumentConstraints();
    if (root) {
      return root;
    } else {
      //Its not there in cache, so we compute it
      if (buCG->FuncCallSiteMap.count(&F)) {
	std::vector<CallSite> &cslist = buCG->FuncCallSiteMap[&F];
	for (unsigned idx = 0, sz = cslist.size(); idx != sz; ++idx) {
	  ABCExprTree *rootCallInst = 0;
	  if (CallInst *CI = dyn_cast<CallInst>(cslist[idx].getInstruction())) {
	    //we need to AND the constraints on the arguments
	    reqArgs = false;
	    Function::arg_iterator formalArgCurrent = F.arg_begin(), formalArgEnd = F.arg_end();
	    for (unsigned i = 1; formalArgCurrent != formalArgEnd; ++formalArgCurrent, ++i) {
	      if (i < CI->getNumOperands()) {
		string varName = getValueName(formalArgCurrent);
		Value *OperandVal = CI->getOperand(i);
		LinearExpr *le = new LinearExpr(OperandVal,Mang);
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
	    if (reqArgs) {
	      //This Call site requires args better to maintain a set
	      //and get the argument constraints once for all
	      //since there could be multiple call sites from the same function
	      reqArgFList.insert(CI->getParent()->getParent());
	    }
	  }
	  if (!root) root = rootCallInst;
	  else {
	    root = new ABCExprTree(root, rootCallInst, "||");
	  }
	}
	std::set<Function *>::iterator sI = reqArgFList.begin(), sE= reqArgFList.end();
	for (; sI != sE ; ++sI) {
	  ABCExprTree * argConstraints = getArgumentConstraints(**sI);
	  if (argConstraints) root = new ABCExprTree(root, argConstraints, "&&");
	}
	fMap[&F]->addArgumentConstraints(root);  //store it in cache
      }
    }
    reqArgs = localSave;
    return root;
  }


  void ArrayBoundsCheck::printStandardArguments(const Module *M, ostream & out) {
    for (Module::const_iterator fI = M->begin(), fE = M->end(); fI != fE; ++fI) {
      if (fI->getName() == "main") {
	Function::const_arg_iterator formalArgCurrent = fI->arg_begin(), formalArgEnd = fI->arg_end();
	if (formalArgCurrent != formalArgEnd) {
	  //relyingon front end's ability to get two arguments
	  string argcname = getValueName(formalArgCurrent);
	  ++formalArgCurrent;
	  string argvname = getValueName(formalArgCurrent);
	  out << " && " << argcname << " = " << argvname ;
	  break;
	}
      }
    }
  }

  void ArrayBoundsCheck::printSymbolicStandardArguments(const Module *M, ostream & out) {
    for (Module::const_iterator fI = M->begin(), fE = M->end(); fI != fE; ++fI) {
      if (fI->getName() == "main") {
	Function::const_arg_iterator formalArgCurrent = fI->arg_begin(), formalArgEnd = fI->arg_end();
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
    if (F.isExternal()) return;
    if (fMap[&F] != 0) {
      MemAccessInstListType MemAccessInstList = fMap[&F]->getMemAccessInstList();
      MemAccessInstListIt maI = MemAccessInstList.begin(), maE = MemAccessInstList.end();
      for (; maI != maE; ++maI) {
	ABCExprTree *root = fMap[&F]->getSafetyConstraint(maI->first);
	ABCExprTree * argConstraints = 0;
	if (maI->second) {
	  argConstraints = getArgumentConstraints(F);
	}
	if (argConstraints) {
	  root = new ABCExprTree(root,argConstraints,"&&");
	}
	//omega stuff should go in here.
	Omega(maI->first,root);
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
      DEBUG(root->printOmegaSymbols(std::cerr));
    }
    printSymbolicStandardArguments(M, out);

    //Dinakar Debug 
    DEBUG(printSymbolicStandardArguments(M,std::cerr));

    out << " P" <<countA << " := {[i] : \n";

    //Dinakar Debug 
    DEBUG(std::cerr << " P" << countA << " := {[i] : \n");
    
    if (root != 0)root->print(out);
    //Dinakar Debug
    DEBUG(if (root != 0)root->print(std::cerr));
    
    printStandardArguments(M, out);
    //Dinakar Debug
    DEBUG(printStandardArguments(M, std::cerr));
    //    out << " && (argv = argc) ";
    out << "};\n Hull P"<<countA++ << ";\n" ;
    
    //Dinakar Debug
    DEBUG(std::cerr << "};\n Hull P"<<countA-1 << ";\n");
    close(parentW);
    int perl2parent[2];
    pipe(perl2parent);
    if (!fork()){
      //child
      close(perl2parent[0]); //perl doesn't read anything from parent
      close(fileno(stdout));
      dup(perl2parent[1]); 
      close(fileno(stdin)); 
      dup(parentR); //this for reading from omega calculator
       if (execvp("/home/vadve/dhurjati/bin/omega.pl",NULL) == -1) {
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
	std::cerr << maI;
	//	UnsafeGetElemPtrs.push_back(maI);	
	//Omega proved SAFE 
      } else {
	std::cerr << "cannot prove safe " << countA;
	std::cerr << maI;
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
    if (execvp("/home/vadve/dhurjati/bin/oc",NULL) == -1) {
      perror("execve error \n");
      exit(-1);
    }
  }
}

bool ArrayBoundsCheck::runOnModule(Module &M) {
  cbudsPass = &getAnalysis<CompleteBUDataStructures>();
  buCG = &getAnalysis<BottomUpCallGraph>();
  Mang = new Mangler(M);
  
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
#ifndef NO_STATIC_CHECK  
  //  out << " Now checking the constraints  \n ";
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
    if (!(provenSafe.count(I) != 0)) checkSafety(*I);
  }
#endif  
  return false;
}


void ArrayBoundsCheck::collectSafetyConstraints(Function &F) {
  if (fMap.count(&F) == 0) {
    fMap[&F] = new FuncLocalInfo();
  }
      
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *iLocal = &*I;
    if (isa<CastInst>(iLocal)) {
      //Some times 
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
#ifdef NO_STATIC_CHECK
      	UnsafeGetElemPtrs.push_back(MAI);
	continue;
#endif      
	  mI++;
	  ABCExprTree *root;
	  string varName = getValueName(MAI->getPointerOperand());
	  LinearExpr *le = new LinearExpr(*mI,Mang);
	  Constraint* c1 = new Constraint(varName,le,"<="); // length < index
	  ABCExprTree* abctemp1 = new ABCExprTree(c1);
	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > index
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(abctemp1, abctemp2, "||");
	  mI++;
	  for (; mI != mE; ++mI) {
	    LinearExpr *le = new LinearExpr(*mI,Mang);
	    varName = varName+"_i" ;
	    Constraint* c1 = new Constraint(varName,le,"<="); // length < index
	    ABCExprTree* abctemp1 = new ABCExprTree(c1);
	    Constraint* c2 = new Constraint("0",le,">",true); // 0 > index
	    ABCExprTree* abctemp2 = new ABCExprTree(c2);
	    ABCExprTree*abctempor = new ABCExprTree(abctemp1,abctemp2,"||"); // abctemp1 || abctemp2
	    root = new ABCExprTree(root, abctempor, "||");
	    //	      }
	  }
	  //reinitialize mI , now getting the constraints on the indices
	  //We need to clear DoneList since we are getting constraints for a
	  //new access. (DoneList is the list of basic blocks that are in the
	  //post dominance frontier of this accesses basic block
	  DoneList.clear();
	  reqArgs = false;
	  addControlDependentConditions(MAI->getParent(), &root);
	  mI = MAI->idx_begin();
	  for (; mI != mE; ++mI) {
	    getConstraints(*mI,&root);
	  }
	  getConstraints(MAI->getPointerOperand(),&root);
	  fMap[&F]->addSafetyConstraint(MAI,root);
	  fMap[&F]->addMemAccessInst(MAI, reqArgs);
	}
      }
    } else if (CallInst *CI = dyn_cast<CallInst>(iLocal)) {
      //Now we need to collect and add the constraints for trusted lib
      //functions like read , fread, memcpy 
      if (Function *FCI = dyn_cast<Function>(CI->getOperand(0))) {
	//Its a direct function call,
	string funcName = FCI->getName();
	DEBUG(std::cerr << "Adding constraints for " << funcName << "\n");
	reqArgs = false;
	if (funcName == "read") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(2));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(2), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	} else if (funcName == "strchr") {
	  std::cerr << " DID NOT HANDLE strchr\n";
	  std::cerr << "Program may not be SAFE\n";
	  //	  exit(-1);
	} else if (funcName == "sprintf") {
	  std::cerr << " DID NOT HANDLE sprintf\n";
	  std::cerr << "Program may not be SAFE\n";
	  //	  abort();
	} else if (funcName == "fscanf") {
	  std::cerr << " DID NOT HANDLE fscanf\n";
	  std::cerr << "Program may not be SAFE\n";
	  //	  abort();
	} else if (funcName == "fread") {
	  //FIXME, assumes reading only a byte 
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	} else if (funcName == "memset") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);
	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	} else if (funcName == "gets") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(1),Mang); //buf.length
	  Constraint* c1 = new Constraint("80",le,"<"); // buf.length > 80  
	  ABCExprTree* root = new ABCExprTree(c1);
	  //	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  //	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  //	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  //	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	    
	} else if (funcName == "llvm.memset") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);
	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	    
	} else if (funcName == "memcpy") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	} else if (funcName == "llvm.memcpy") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(3),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(3), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	} else if (funcName == "strcpy") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(2),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,"<="); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  getConstraints(CI->getOperand(2), &root);
	  getConstraints(CI->getOperand(1), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);
	    
	} else if (funcName == "snprintf") {
	  LinearExpr *le = new LinearExpr(CI->getOperand(2),Mang);
	  string varName = getValueName(CI->getOperand(1));
	  Constraint* c1 = new Constraint(varName,le,">"); // buf.length >= size 
	  ABCExprTree* root = new ABCExprTree(c1);

	  Constraint* c2 = new Constraint("0",le,">",true); // 0 > size
	  ABCExprTree* abctemp2 = new ABCExprTree(c2);
	  root = new ABCExprTree(root,abctemp2,"||"); // abctemp1 || abctemp2	    
	  getConstraints(CI->getOperand(1), &root);
	  getConstraints(CI->getOperand(2), &root);
	  fMap[&F]->addMemAccessInst(CI, reqArgs);
	  fMap[&F]->addSafetyConstraint(CI, root);

	} else if (funcName == "fprintf") {
	  if (!isa<ConstantArray>(CI->getOperand(2))) {
	    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(CI->getOperand(2))) {
	      if (!isa<ConstantArray>(GEPI->getPointerOperand())) {
		std::cerr << "Format string problem " << CI->getOperand(2);
		//exit(-1);
	      }
	    }
	  }
	} else if (funcName == "vfprintf") {
	  if (!isa<ConstantArray>(CI->getOperand(2))) {
	    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(CI->getOperand(2))) {
	      if (!isa<ConstantArray>(GEPI->getPointerOperand())) {
		std::cerr << "Format string problem " << CI->getOperand(2);
		//		exit(-1);
	      }
	    }
	  }
	} else if (funcName == "printf") {
	  if (!isa<ConstantArray>(CI->getOperand(1))) {
	    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(CI->getOperand(1))) {
	      if (!isa<ConstantArray>(GEPI->getPointerOperand())) {
		std::cerr << "Format string problem " << CI->getOperand(1);
		//exit(-1);
	      }
	    }
	  }
	} else if (funcName == "syslog") {
	  if (!isa<ConstantArray>(CI->getOperand(2))) {
	    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(CI->getOperand(1))) {
	      if (!isa<ConstantArray>(GEPI->getPointerOperand())) {
		std::cerr << "Format string problem " << CI->getOperand(1);
		//exit(-1);
	      }
	    }
	  }
	} else if (FCI->isExternal()) {
	  if (KnownFuncDB.find(funcName) == KnownFuncDB.end()) {
            //	    std::cerr << "Don't know what constraints to add " << funcName << "\n";
	    //	    std::cerr << "Exiting \n";
	    //	    exit(-1);
	  }
	}
      } else {
	//indirect function call doesn't call the known external functions
	CompleteBUDataStructures::callee_iterator
	  cI = cbudsPass->callee_begin(CI), cE = cbudsPass->callee_end(CI);
	for (; cI != cE; ++cI) { 
	  if ((cI->second->isExternal()) || (KnownFuncDB.find(cI->second->getName()) != KnownFuncDB.end())) {
	    assert(1 && " Assumption that indirect fn call doesnt call an externalfails");
	  }
	}
      }
    }
  }
}

void ArrayBoundsCheck::addBranchConstraints(BranchInst *BI,BasicBlock *Successor, ABCExprTree **rootp) {
  //this has to be a conditional branch, otherwise we wouldnt have been here
  assert((BI->isConditional()) && "abcd wrong branch constraint");
  if (SetCondInst *SCI = dyn_cast<SetCondInst>(BI->getCondition())) {

    //SCI now has the conditional statement
    Value *operand0 = SCI->getOperand(0);
    Value *operand1 = SCI->getOperand(1);

    getConstraints(operand0,rootp);
    getConstraints(operand1,rootp);
      

    LinearExpr *l1 = new LinearExpr(operand1,Mang);

    string var0 = getValueName(operand0);
    Constraint *ct = 0;

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
    if (ct != 0) {
      ct->print(std::cerr);
      *rootp = new ABCExprTree(*rootp,new ABCExprTree(ct),"&&");
    }
  }
}



// SimplifyExpression: Simplify a Value and return it as an affine expressions
LinearExpr* ArrayBoundsCheck::SimplifyExpression( Value *Expr, ABCExprTree **rootp) {
  assert(Expr != 0 && "Can't classify a null expression!");
  if (Expr->getType() == Type::FloatTy || Expr->getType() == Type::DoubleTy)
    return new LinearExpr(Expr, Mang) ;   // nothing  known return variable itself

  if ((isa<BasicBlock>(Expr)) || (isa<Function>(Expr)))
    assert(1 && "Unexpected expression type to classify!");
  if ((isa<GlobalVariable>(Expr)) || (isa<Argument>(Expr))) {
    reqArgs = true; //I know using global is ugly, fix this later 
    return new LinearExpr(Expr, Mang);
  }
  if (isa<Constant>(Expr)) {
    Constant *CPV = cast<Constant>(Expr);
    if (CPV->getType()->isIntegral()) { // It's an integral constant!
      if (ConstantArray *CA = dyn_cast<ConstantArray>(CPV)) {
	assert(1 && "Constant Array don't know how to get the values ");
      } else if (ConstantInt *CPI = dyn_cast<ConstantInt>(Expr)) {
	return new LinearExpr(CPI, Mang);
      }
    }
    return new LinearExpr(Expr, Mang); //nothing known, just return itself
  }
  if (isa<Instruction>(Expr)) {
    Instruction *I = cast<Instruction>(Expr);
    const Type *Ty = I->getType();

    switch (I->getOpcode()) {       // Handle each instruction type seperately
    case Instruction::Add: {
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0), rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), Mang);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1), rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), Mang);
      }
      Left->addLinearExpr(Right);
      return Left;
    }
    case Instruction::Sub: {
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0), rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), Mang);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1), rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), Mang);
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
      LinearExpr* L = new LinearExpr(I->getOperand(1),Mang);
      return L;
    };
    case Instruction::Mul :
    
      LinearExpr* Left =  (SimplifyExpression(I->getOperand(0),rootp));
      if (Left == 0) {
	Left = new LinearExpr(I->getOperand(0), Mang);
      }
      LinearExpr* Right = (SimplifyExpression(I->getOperand(1),rootp));
      if (Right == 0) {
	Right = new LinearExpr(I->getOperand(1), Mang);
      }
      return Left->mulLinearExpr(Right);
    }  // end switch
    if (isa<CastInst>(I)) {
      DEBUG(std::cerr << "dealing with cast instruction ");
      const Type *fromType = I->getOperand(0)->getType();
      const Type *toType = I->getType();
      string number1;
      string number2;
      bool addC = false;
      if (toType->isPrimitiveType() && fromType->isPrimitiveType()) {
	//Here we have to give constraints for 
	//FIXME .. this should be for all types not just sbyte 
	switch(toType->getTypeID()) {
	case Type::IntTyID :
	  switch (fromType->getTypeID()) {
	  case Type::SByteTyID :
	    number1 = "-128";
	    number2 = "127";
	    addC = true;
	    break;
	  case Type::UByteTyID :
	    number1 = "0";
	    number2 = "255";
	    addC = true;
	  default:
	    break;
	  }
	case Type::UIntTyID :
	  switch(fromType->getTypeID()) {
	  case Type::IntTyID :
	    //in llvm front end the malloc argument is always casted to
	    //uint! so we hack it here
	    //This hack works incorrectly for
	    //some programs so moved it to malloc itself
	    //FIXME FIXME This might give incorrect results in some cases!!!!
	    addC = true;
	    break;
	  case Type::SByteTyID :
	  case Type::UByteTyID :
	    number1 = "0";
	    number2 = "255";
	    addC = true;
	    break;
	  default :
	    break;
	  }
	default:
	  break;
	}
	if (addC) {
	  string var = getValueName(I);
	  LinearExpr *l1 = new LinearExpr(I,Mang);
	  if (number1 != "") {
	    Constraint *c1 = new Constraint(number1,l1,">=",true);
	    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c1),"&&");
	  }
	  if (number2 != "") {
	    Constraint *c2 = new Constraint(number2,l1,"<=",true);
	    *rootp = new ABCExprTree(*rootp,new ABCExprTree(c2),"&&");
	  }
	  Constraint *c = new Constraint(var, SimplifyExpression(I->getOperand(0),rootp),"=");
	  *rootp = new ABCExprTree(*rootp,new ABCExprTree(c),"&&");
	  return l1;
	}
      } else {
	if (const PointerType *pType = dyn_cast<PointerType>(I->getType())){
	  const Type *eltype = pType->getElementType();
	  if (eltype->isPrimitiveType()) {
	    unsigned numbytes = 0;
	    if (eltype->getTypeID() == Type::SByteTyID) {
	    //FIXME: this should make use of Target!!!!
	      numbytes = 1;
	    } else if (eltype->getTypeID() == Type::UByteTyID) {
	      numbytes = 4;
	    } else if (eltype->getTypeID() == Type::IntTyID) {
	      numbytes = 4;
	    } else if (eltype->getTypeID() == Type::UIntTyID) {
	      numbytes = 4;
	    } else if (eltype->getTypeID() == Type::ShortTyID) {
	      numbytes = 2;
	    } else if (eltype->getTypeID() == Type::UShortTyID) {
	      numbytes = 2;
	    } else if (eltype->getTypeID() == Type::LongTyID) {
	      numbytes = 8;
	    } else if (eltype->getTypeID() == Type::ULongTyID) {
	      numbytes = 8;
	    } 

	    if (numbytes != 0) {
	      if (const PointerType *opPType = dyn_cast<PointerType>(fromType)){
		const Type *opEltype = opPType->getElementType();
		if (const StructType *stype = dyn_cast<StructType>(opEltype)) {
		  //special case for casts to beginning of structs
		  // the case is (sbyte*) (Struct with the first element arrauy)
		  //If it is a cast from struct to something else * ...
		  if (const ArrayType *aType = dyn_cast<ArrayType>(stype->getContainedType(0))) {
		    if (aType->getElementType()->isPrimitiveType()) {
		      int elSize = aType->getNumElements();
		      switch (aType->getElementType()->getTypeID()) {
		      case Type::ShortTyID :
		      case Type::UShortTyID :  elSize = (elSize/numbytes)*2; break;
		      case Type::IntTyID :
		      case Type::UIntTyID :  elSize = (elSize/numbytes)*4; break;
		      case Type::LongTyID :
		      case Type::ULongTyID :  elSize = (elSize/numbytes)*8; break;
		      default : break;
		      }
		      string varName = getValueName(I);
		      const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
		      const ConstantSInt * signedOne = ConstantSInt::get(csiType,elSize);
		      LinearExpr *l1 = new LinearExpr(signedOne, Mang);
		      return l1;
		    }
		  }
		} else if (const ArrayType *aType = dyn_cast<ArrayType>(opEltype)) {
		  if (aType->getElementType()->isPrimitiveType()) {
		    int elSize = aType->getNumElements();
		    switch (aType->getElementType()->getTypeID()) {
		    case Type::SByteTyID : 
		    case Type::UByteTyID : elSize = elSize / numbytes; break;
		    case Type::ShortTyID :
		    case Type::UShortTyID :  elSize = (elSize/numbytes) *2; break;
		    case Type::IntTyID :
		    case Type::UIntTyID :  elSize = (elSize/numbytes)*4; break;
		    case Type::LongTyID :
		    case Type::ULongTyID :  elSize = (elSize/numbytes)*8; break;
		    default : break;
		    }
		    string varName = getValueName(I);
		    const Type* csiType = Type::getPrimitiveType(Type::IntTyID);
		    const ConstantSInt * signedOne = ConstantSInt::get(csiType,elSize);
		    LinearExpr *l1 = new LinearExpr(signedOne, Mang);
		    return l1;
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
      LinearExpr* ret = new LinearExpr(I,Mang);
      return ret;
    }
  }
  // Otherwise, I don't know anything about this value!
  return 0;
}


Pass *createArrayBoundsCheckPass() { return new ABC::ArrayBoundsCheck(); }
