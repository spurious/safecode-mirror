//===- InstrumentMemoryAccesses.cpp - Insert load/store checks ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass instruments loads, stores, and other memory intrinsics with
// load/store checks by inserting the relevant __loadcheck and/or
// __storecheck calls before the them.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "instrument-memory-accesses"

#include "CommonMemorySafetyPasses.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Constants.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Instrumentation.h"

using namespace llvm;

STATISTIC(LoadsInstrumented, "Loads instrumented");
STATISTIC(StoresInstrumented, "Stores instrumented");
STATISTIC(AtomicsInstrumented, "Atomic memory intrinsics instrumented");
STATISTIC(IntrinsicsInstrumented, "Block memory intrinsics instrumented");

namespace {
  class InstrumentMemoryAccesses : public FunctionPass,
                                   public InstVisitor<InstrumentMemoryAccesses> {
    const TargetData *TD;
    IRBuilder<> *Builder;

    PointerType *VoidPtrTy;
    IntegerType *SizeTy;

    Function *LoadCheckFunction;
    Function *StoreCheckFunction;

    void instrument(Value *Pointer, Value *AccessSize, Function *Check,
                    Instruction &I);

  public:
    static char ID;
    InstrumentMemoryAccesses(): FunctionPass(ID) { }
    virtual bool doInitialization(Module &M);
    virtual bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.setPreservesCFG();
    }

    virtual const char *getPassName() const {
      return "InstrumentMemoryAccesses";
    }

    // Visitor methods
    void visitLoadInst(LoadInst &LI);
    void visitStoreInst(StoreInst &SI);
    void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I);
    void visitAtomicRMWInst(AtomicRMWInst &I);
    void visitMemIntrinsic(MemIntrinsic &MI);
  };
} // end anon namespace

char InstrumentMemoryAccesses::ID = 0;

INITIALIZE_PASS(InstrumentMemoryAccesses, "instrument-memory-accesses",
                "Instrument memory accesses", false, false)

FunctionPass *llvm::createInstrumentMemoryAccessesPass() {
  return new InstrumentMemoryAccesses();
}

bool InstrumentMemoryAccesses::doInitialization(Module &M) {
  Type *VoidTy = Type::getVoidTy(M.getContext());
  VoidPtrTy = Type::getInt8PtrTy(M.getContext());
  SizeTy = IntegerType::getInt64Ty(M.getContext());

  // Create function prototypes
  M.getOrInsertFunction("__loadcheck", VoidTy, VoidPtrTy, SizeTy, NULL);
  M.getOrInsertFunction("__storecheck", VoidTy, VoidPtrTy, SizeTy, NULL);
  return true;
}

bool InstrumentMemoryAccesses::runOnFunction(Function &F) {
  // Check that the load and store check functions are declared.
  LoadCheckFunction = F.getParent()->getFunction("__loadcheck");
  assert(LoadCheckFunction && "__loadcheck function has disappeared!\n");

  StoreCheckFunction = F.getParent()->getFunction("__storecheck");
  assert(StoreCheckFunction && "__storecheck function has disappeared!\n");

  TD = &getAnalysis<TargetData>();
  IRBuilder<> TheBuilder(F.getContext());
  Builder = &TheBuilder;

  // Visit all of the instructions in the function.
  visit(F);
  return true;
}

void InstrumentMemoryAccesses::instrument(Value *Pointer, Value *AccessSize,
                                          Function *Check, Instruction &I) {
  Builder->SetInsertPoint(&I);
  Value *VoidPointer = Builder->CreatePointerCast(Pointer, VoidPtrTy);
  CallInst *CI = Builder->CreateCall2(Check, VoidPointer, AccessSize);

  // Copy debug information if it is present.
  if (MDNode *MD = I.getMetadata("dbg"))
    CI->setMetadata("dbg", MD);
}

void InstrumentMemoryAccesses::visitLoadInst(LoadInst &LI) {
  // Instrument a load instruction with a load check.
  Value *AccessSize = ConstantInt::get(SizeTy,
                                       TD->getTypeStoreSize(LI.getType()));
  instrument(LI.getPointerOperand(), AccessSize, LoadCheckFunction, LI);
  ++LoadsInstrumented;
}

void InstrumentMemoryAccesses::visitStoreInst(StoreInst &SI) {
  // Instrument a store instruction with a store check.
  uint64_t Bytes = TD->getTypeStoreSize(SI.getValueOperand()->getType());
  Value *AccessSize = ConstantInt::get(SizeTy, Bytes);
  instrument(SI.getPointerOperand(), AccessSize, StoreCheckFunction, SI);
  ++StoresInstrumented;
}

void InstrumentMemoryAccesses::visitAtomicRMWInst(AtomicRMWInst &I) {
  // Instrument an AtomicRMW instruction with a store check.
  Value *AccessSize = ConstantInt::get(SizeTy,
                                       TD->getTypeStoreSize(I.getType()));
  instrument(I.getPointerOperand(), AccessSize, StoreCheckFunction, I);
  ++AtomicsInstrumented;
}

void InstrumentMemoryAccesses::visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
  // Instrument an AtomicCmpXchg instruction with a store check.
  Value *AccessSize = ConstantInt::get(SizeTy,
                                       TD->getTypeStoreSize(I.getType()));
  instrument(I.getPointerOperand(), AccessSize, StoreCheckFunction, I);
  ++AtomicsInstrumented;
}

void InstrumentMemoryAccesses::visitMemIntrinsic(MemIntrinsic &MI) {
  // Instrument llvm.mem[set|cpy|move].* calls with load/store checks.
  Builder->SetInsertPoint(&MI);
  Value *AccessSize = Builder->CreateIntCast(MI.getLength(), SizeTy,
                                             /*isSigned=*/false);

  // memcpy and memmove have a source memory area but memset doesn't
  if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(&MI))
    instrument(MTI->getSource(), AccessSize, LoadCheckFunction, MI);
  instrument(MI.getDest(), AccessSize, StoreCheckFunction, MI);
  ++IntrinsicsInstrumented;
}
