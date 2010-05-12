//===- PoolHandles.cpp - Passes for finding pointer attributes for SAFECode --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements several passes which ease the use of the automatic pool
// allocation transform.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "poolhandle"

#include "safecode/Config/config.h"
#include "safecode/PoolHandles.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/Support/AllocatorInfo.h"

NAMESPACE_SC_BEGIN

// Pass ID variables
char PoolMDPass::ID       = 0;
char QueryPoolPass::ID    = 0;
char RemovePoolMDPass::ID = 0;

static RegisterPass<PoolMDPass>
X ("pool-md", "Insert meta-data about pool allocation");

static RegisterPass<QueryPoolPass>
Y ("querypool", "Query pool meta-data");

static RegisterPass<RemovePoolMDPass>
Z ("remove-poolmd", "Remove meta-data about pool allocation");

//
// Method: createPoolMetaData()
//
// Description:
//  This method locates the pool for the specified value and creates a metadata
//  node that links the value with its pool.
//
// Inputs:
//  GV - The global variable for which a pool metadata node should be created.
//
// Side effects:
//  This method will add the metata node to a container that is global to class
//  member methods.
//
void
PoolMDPass::createPoolMetaData (GlobalVariable * GV) {
  //
  // Get the DSNode information associated with the value.
  //
  DSNode * Node  = dsnPass->getDSNodeForGlobalVariable (GV);
  assert (Node && "No DSNode for global!\n");

  //
  // Get the pool associated with the value.
  //
  Value * PH = dsnPass->paPass->getGlobalPool (Node);
  assert (PH && "No pool handle for the global variable!\n");

  //
  // Create LLVM values representing the pointer's type, DSNode Flags, etc.
  //
  LLVMContext & Context = GV->getContext();
  const Type * Int1Type  = Type::getInt1Ty (Context);
  const Type * Int32Type = Type::getInt32Ty(Context);
  Value * IsFolded = ConstantInt::get(Int1Type, Node->isNodeCompletelyFolded());
  Value * DSFlags = ConstantInt::get(Int32Type, Node->getNodeFlags());

  // 
  // Create a new metadata node that contains the pool handle and the value.
  // 
  Value * PoolMap[4] = {GV, PH, IsFolded, DSFlags};
  MDNode * MD = MDNode::get (Context, PoolMap, 4);

  //
  // Add the value to pool mapping to the set of mappings we've created so far.
  //
  ValueToPoolNodes.push_back (MD);
  return;
}

//
// Method: createPoolMetaData()
//
// Description:
//  This method locates the pool for the specified value and creates a metadata
//  node that links the value with its pool.
//
// Inputs:
//  V - The value for which a pool metadata node should be created.
//  F - The function in which the pool should be found.
//
// Side effects:
//  This method will add the metata node to a container that is global to class
//  member methods.
//
void
PoolMDPass::createPoolMetaData (Value * V, Function * F) {
  //
  // Get the pool associated with the value.
  //
  V = V->stripPointerCasts();
  PA::FuncInfo *FI = dsnPass->paPass->getFuncInfoOrClone(*F);
  Value * PH = dsnPass->getPoolHandle (V, F, *FI);
  assert (PH && "No pool handle for the specified value!\n");

  //
  // Get the DSNode information associated with the value.
  //
  DSNode* Node = dsnPass->getDSNode(V, F);
  assert (Node && "Value has no DSNode!\n");

  //
  // Create LLVM values representing the pointer's type, DSNode Flags, etc.
  //
  const Type * Int1Type  = Type::getInt1Ty (F->getParent()->getContext());
  const Type * Int32Type = Type::getInt32Ty(F->getParent()->getContext());
  Value * IsFolded = ConstantInt::get(Int1Type, Node->isNodeCompletelyFolded());
  Value * DSFlags = ConstantInt::get(Int32Type, Node->getNodeFlags());

  // 
  // Create a new metadata node that contains the pool handle and the value.
  // 
  Value * PoolMap[4] = {V, PH, IsFolded, DSFlags};
  MDNode * MD = MDNode::get (F->getParent()->getContext(), PoolMap, 4);

  //
  // Add the value to pool mapping to the set of mappings we've created so far.
  //
  ValueToPoolNodes.push_back (MD);
  return;
}

//
// Method: createOffsetMetaData()
//
// Description:
//  This method generates meta-data that describes the offset into an object
//  from which the result of a load is taken.
//
void
PoolMDPass::createOffsetMetaData (LoadInst & LI) {
  //
  // Get the DSNode information for both the result of the load instruction
  // and its pointer operand.  If the result of the load has no DSNode, then
  // nothing needs to be done.
  //
  Function * F = LI.getParent()->getParent();
  DSNode* ResultNode = dsnPass->getDSNode (&LI, F);
  DSNode* PtrNode    = dsnPass->getDSNode (LI.getPointerOperand(), F);
  if (!ResultNode) return;
  assert (PtrNode && "Load operand has no DSNode!\n");

  //
  // Scan through the links of the DSNode of the load's pointer operand; we
  // need to determine the offset into the memory object from which the result
  // of the load is loaded.
  //
  DSNode::LinkMapTy::iterator linki = PtrNode->edge_begin();
  for (; linki != PtrNode->edge_end(); ++linki) {
    DSNodeHandle & LinkNode = linki->second;
    if (LinkNode.getNode() == ResultNode) {
      //
      // Get the offset from the DSNodeHandle.
      //
      unsigned offset = LinkNode.getOffset();

      //
      // Create meta-data describing the offset of the pointer loaded by the
      // load instruction.
      //
      const Type * Int32Type = Type::getInt32Ty(F->getParent()->getContext());
      Value * Alignment = ConstantInt::get(Int32Type, offset);
      Value * AlignData[2] = {&LI, Alignment};
      MDNode * MD = MDNode::get (F->getParent()->getContext(), AlignData, 2);

      //
      // Add the alignment metadata to the set of alignment metadata notdes.
      //
      AlignmentNodes.push_back (MD);
    }
  }

  return;
}

void
PoolMDPass::visitAllocaInst(AllocaInst & AI) {
  //
  // Create meta-data linking the allocated pointer with its pool.
  //
  Function * F = AI.getParent()->getParent();
  createPoolMetaData (&AI, F);
  return;
}

void
PoolMDPass::visitLoadInst (LoadInst & LI) {
  //
  // Create meta-data linking the dereferenced pointer with its pool.
  //
  Function * F = LI.getParent()->getParent();
  createPoolMetaData (LI.getPointerOperand(), F);
  return;
}

void
PoolMDPass::visitStoreInst (StoreInst & SI) {
  //
  // Create meta-data linking the dereferenced pointer with its pool.
  //
  Function * F = SI.getParent()->getParent();
  createPoolMetaData (SI.getPointerOperand(), F);
  return;
}

void
PoolMDPass::visitGetElementPtrInst (GetElementPtrInst & GEP) {
  //
  // Create meta-data linking the dereferenced pointer with its pool.
  //
  Function * F = GEP.getParent()->getParent();
  createPoolMetaData (&GEP, F);
  return;
}

void
PoolMDPass::visitICmpInst (ICmpInst & CI) {
  //
  // Create meta-data for the operands of the compare instruction if they are
  // pointers.  The RewriteOOB pass may look up their pools.
  //
  Function * F = CI.getParent()->getParent();

  if (isa<PointerType>(CI.getOperand(0)->getType()))
    if (!isa<ConstantPointerNull>(CI.getOperand(0)))
      createPoolMetaData (CI.getOperand(0), F);

  if (isa<PointerType>(CI.getOperand(1)->getType()))
    if (!isa<ConstantPointerNull>(CI.getOperand(1)))
      createPoolMetaData (CI.getOperand(1), F);
  return;
}

void
PoolMDPass::visitPtrToIntInst(PtrToIntInst &I) {
  //
  // Create meta-data linking the casted pointer with its pool.
  //
  Function * F = I.getParent()->getParent();
  if (isa<PointerType>(I.getOperand(0)->getType()))
    createPoolMetaData (I.getOperand(0), F);
  return;
}

void
PoolMDPass::visitCallInst (CallInst &CI) {
  //
  // Get the called function.  If this is an indirect call, then ignore it.
  //
  Function * CalledFunc = CI.getCalledFunction();
  if (!CalledFunc) return;

  //
  // Determine whether this is a call to an allocator or deallocator.  If it is,
  // then record the pool information for the allocated or deallocated pointer.
  //
  SAFECodeConfiguration::alloc_iterator it  = SCConfig.alloc_begin(),
                                        end = SCConfig.alloc_end();
  for (; it != end; ++it) {
    // Handle a call to an allocator
    AllocatorInfo * AllocInfo = *it;
    if (AllocInfo->getAllocCallName() == CalledFunc->getNameStr()) {
      createPoolMetaData (&CI, CI.getParent()->getParent());
    }

    // Handle a call to a deallocator
    if (AllocInfo->getFreeCallName() == CalledFunc->getNameStr()) {
      Value * Pointer = AllocInfo->getFreedPointer (&CI);
      createPoolMetaData (Pointer, CI.getParent()->getParent());
    }
  }

  //
  // Create meta-data linking the dereferenced pointer with its pool.
  //
  return;
}

//
// Method: createGlobalMetaData()
//
// Description:
//  This method scans over all global variables in the program and creates pool
//  metadata for those globals that interest the SAFECode passes.
//
void
PoolMDPass::createGlobalMetaData (Module & M) {
  //
  // Scan through all global variables and get their pool handles.
  //
  Module::global_iterator GI = M.global_begin(), GE = M.global_end();
  for ( ; GI != GE; ++GI) {
    //
    // Skip anything that is not a global variable (e.g., functions).
    //
    GlobalVariable *GV = dyn_cast<GlobalVariable>(GI);
    if (!GV) continue;

    //
    // Skip pool descriptors.
    //
    if (GV->getType()->getContainedType(0) == dsnPass->getPoolType()) continue;

    //
    // Skip debug metadata and other LLVM metadata.
    //
    std::string name = GV->getName();
    if ((GV->getSection()) == "llvm.metadata") continue;
    if (strncmp(name.c_str(), "llvm.", 5) == 0) continue;

    //
    // Create metadata for the global.
    //
    createPoolMetaData (GV);
  }
}

//
// Method: createByValMetaData()
//
// Description:
//  Scan through all functions in the specified module and create metadata for
//  all of the byval arguments of each function.
//
// Inputs:
//  M - The module to modify.
//
// Outputs:
//  M - The modified module with metadata mapping byval arguments to their
//      pools and their DSNode information.
//
void
PoolMDPass::createByValMetaData (Module & M) {
  //
  // Scan through each function looking for byval arguments.
  //
  Module::iterator FI = M.begin(), FE = M.end();
  for ( ; FI != FE; ++FI) {
    Function::arg_iterator Arg = FI->arg_begin();
    for (; Arg != FI->arg_end(); ++Arg) {
      if (Arg->hasByValAttr()) {
        createPoolMetaData (Arg, FI);
      }
    }
  }

  return;
}

//
// Method: runOnModule()
//
// Description:
//  The LLVM pass manager will call this method when this pass is to be run on
//  a Module.
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
PoolMDPass::runOnModule (Module &M) {
  //
  // Get a handle to the pool allocation pass and other passes which we
  // require.
  //
  dsnPass    = &getAnalysis<DSNodePass>();

  //
  // Create metadata for global varibles.
  //
  createGlobalMetaData (M);

  //
  // Create metadata for function byval arguments.
  //
  createByValMetaData (M);

  //
  // Visit all instructions within the module to find all of the pool handles
  // we need.
  //
  visit (M);

  //
  // Create a global meta-data node that links to all the other meta-data.
  //
  Twine name ("SCValueMap");
  NamedMDNode * MD = NamedMDNode::Create (M.getContext(), name, 0, 0, &M);
  for (unsigned index = 0; index < ValueToPoolNodes.size(); ++index)
    MD->addOperand (ValueToPoolNodes[index]);

  //
  // Create a global meta-data node that links to all the alignment meta-data.
  //
  Twine name2 ("SCAlignData");
  NamedMDNode * AMD = NamedMDNode::Create (M.getContext(), name2, 0, 0, &M);
  for (unsigned index = 0; index < AlignmentNodes.size(); ++index)
    AMD->addOperand (AlignmentNodes[index]);

  // Assume that we modified something
  return true;
}

bool
QueryPoolPass::runOnModule (Module & M) {
  //
  // Get the pool mapping metadata, scan through all of the metadata, and add
  // the information in it into our internal data structures.
  //
  if (const NamedMDNode * MD = M.getNamedMetadata ("SCValueMap")) {
    for (unsigned index = 0; index < MD->getNumOperands(); ++index) {
      //
      // Get one entry of meta-data.
      //
      MDNode * Node = dyn_cast<MDNode>(MD->getOperand (index));
      assert (Node && "Wrong type of meta data!\n");

      //
      // Extract the information about this value from the metadata.
      //
      Value * V              = dyn_cast<Value>(Node->getOperand (0));
      Value * PH             = dyn_cast<Value>(Node->getOperand (1));
      ConstantInt * IsFolded = dyn_cast<ConstantInt>(Node->getOperand (2));
      ConstantInt * DSFlags  = dyn_cast<ConstantInt>(Node->getOperand (3));

      //
      // Do some assertions to make sure that everything is sane.
      //
      assert (V  && "MDNode first element is not a Value!\n");
      assert (PH && "MDNode second element is not a Pool Handle!\n");
      assert (IsFolded && "MDNode third element is not a constant integer!\n");
      assert (DSFlags && "MDNode fourth element is not a constant integer!\n");

      //
      // Add the values into the maps.
      //
      PoolMap[V]   = PH;
      FoldedMap[V] = !(IsFolded->isZero());
      FlagMap[V]   = DSFlags->getZExtValue();
    }
  }

  //
  // Get the alignment metadata, scan through all of the metadata, and add
  // the information in it into our internal data structures.
  //
  if (const NamedMDNode * MD = M.getNamedMetadata ("SCAlignData")) {
    for (unsigned index = 0; index < MD->getNumOperands(); ++index) {
      //
      // Get one entry of meta-data.
      //
      MDNode * Node = dyn_cast<MDNode>(MD->getOperand (index));
      assert (Node && "Wrong type of meta data!\n");

      //
      // Extract the information about this value from the metadata.
      //
      LoadInst * LI           = dyn_cast<LoadInst>(Node->getOperand (0));
      ConstantInt * Alignment = dyn_cast<ConstantInt>(Node->getOperand (1));

      //
      // Do some assertions to make sure that everything is sane.
      //
      assert (LI && "MDNode first element is not a load instruction!\n");
      assert (Alignment && "MDNode second element is not an integer!\n");

      //
      // Add the values into the maps.
      //
      AlignMap[LI] = Alignment;
    }
  }

  return false;
}

//
// Method: getPool()
//
// Description:
//  Given an LLVM value, attempt to find the pool associated with that value.
//
Value *
QueryPoolPass::getPool (const Value * V) {
  return PoolMap[V->stripPointerCasts()];
}

//
// Method: getPoolType()
//
// Description:
//  Return the type of a pool.
//
const Type *
QueryPoolPass::getPoolType (void) {
  return PoolMap.begin()->second->getType();
}

//
// Method: runOnModule()
//
// Description:
//  This is the entry point for our pass.  It removes the metadata created by
//  PoolMDPass.
//
// Return value:
//  true  - Metadata was removed from the Module.
//  false - No modifications were made to the Module.
//
bool
RemovePoolMDPass::runOnModule (Module & M) {
  //
  // Get the pool metadata.  If there isn't any metadata, then nothing needs to
  // be done.
  //  
  NamedMDNode * MD  = M.getNamedMetadata ("SCValueMap");
  NamedMDNode * AMD = M.getNamedMetadata ("SCAlignData");
  
  //
  // Remove the metadata.
  //
  if (MD)   MD->eraseFromParent();
  if (AMD) AMD->eraseFromParent();
      
  //    
  // Assume we always modify the module.
  //
  return true;
} 

NAMESPACE_SC_END

