//=== SoftBound/InitializeSoftBound.cpp - Helper Pass for SoftBound/CETS --*- C++ -*===// 
// Copyright (c) 2011 Santosh Nagarakatte, Milo M. K. Martin. All rights reserved.

// Developed by: Santosh Nagarakatte, Milo M.K. Martin,
//               Jianzhou Zhao, Steve Zdancewic
//               Department of Computer and Information Sciences,
//               University of Pennsylvania
//               http://www.cis.upenn.edu/acg/softbound/

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal with the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

//   1. Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimers.

//   2. Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimers in the
//      documentation and/or other materials provided with the distribution.

//   3. Neither the names of Santosh Nagarakatte, Milo M. K. Martin,
//      Jianzhou Zhao, Steve Zdancewic, University of Pennsylvania, nor
//      the names of its contributors may be used to endorse or promote
//      products derived from this Software without specific prior
//      written permission.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// WITH THE SOFTWARE.
//===---------------------------------------------------------------------===//

#include "SoftBound/InitializeSoftBound.h"


char InitializeSoftBound:: ID = 0;

static RegisterPass<InitializeSoftBound> P ("InitializeSoftBound",
                                            "Prototype Creator Pass for SoftBound");

void InitializeSoftBound:: constructShadowStackHandlers(Module & module){

  Type* VoidTy = Type::getVoidTy(module.getContext());
  Type* VoidPtrTy = PointerType::getUnqual(Type::getInt8Ty(module.getContext()));
  Type* SizeTy = Type::getInt64Ty(module.getContext());
  
  Type* Int32Ty = Type::getInt32Ty(module.getContext());
  module.getOrInsertFunction("__softboundcets_allocate_shadow_stack_space", VoidTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_deallocate_shadow_stack_space", VoidTy, NULL);
  module.getOrInsertFunction("__softboundcets_load_base_shadow_stack", VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_load_bound_shadow_stack", VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_load_key_shadow_stack", SizeTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_load_lock_shadow_stack", VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_store_base_shadow_stack", VoidTy, VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_store_bound_shadow_stack", VoidTy, VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_store_key_shadow_stack", VoidTy, SizeTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_store_lock_shadow_stack", VoidTy, VoidPtrTy, Int32Ty, NULL);

}

void InitializeSoftBound:: constructMetadataHandlers(Module & module){

  Type* VoidTy = Type::getVoidTy(module.getContext());
  Type* VoidPtrTy = PointerType::getUnqual(Type::getInt8Ty(module.getContext()));
  Type* SizeTy = Type::getInt64Ty(module.getContext());
  
  Type* Int32Ty = Type::getInt32Ty(module.getContext());

  module.getOrInsertFunction("__softboundcets_introspect_metadata", VoidTy, VoidPtrTy, VoidPtrTy, Int32Ty, NULL);
  module.getOrInsertFunction("__softboundcets_copy_metadata", VoidTy, VoidPtrTy, VoidPtrTy, SizeTy, NULL);

  Type* PtrVoidPtrTy = PointerType::getUnqual(VoidPtrTy);
  Type* PtrSizeTy = PointerType::getUnqual(SizeTy);
  
  module.getOrInsertFunction("__softboundcets_metadata_load", VoidTy, VoidPtrTy, PtrVoidPtrTy, PtrVoidPtrTy, PtrSizeTy, PtrVoidPtrTy, NULL);

  module.getOrInsertFunction("__softboundcets_metadata_store", VoidTy, VoidPtrTy, VoidPtrTy, VoidPtrTy, SizeTy, VoidPtrTy, NULL);

  module.getOrInsertFunction("__softboundcets_get_global_lock", VoidPtrTy, NULL);

  module.getOrInsertFunction("__softboundcets_stack_memory_allocation", VoidTy, VoidPtrTy, PtrVoidPtrTy, PtrSizeTy, NULL);

  module.getOrInsertFunction("__softboundcets_stack_memory_deallocation", VoidTy, SizeTy, NULL);

  module.getOrInsertFunction("__softboundcets_spatial_call_dereference_check", VoidTy, VoidPtrTy, VoidPtrTy, VoidPtrTy, NULL);


}

void InitializeSoftBound:: constructCheckHandlers(Module & module){

  Type* void_ty = Type::getVoidTy(module.getContext());

  Type* void_ptr_ty = PointerType::getUnqual(Type::getInt8Ty(module.getContext()));
  Type* size_ty = Type::getInt64Ty(module.getContext());

  Function* spatial_load_check = (Function *) module.getOrInsertFunction("__softboundcets_spatial_load_dereference_check", void_ty, void_ptr_ty, void_ptr_ty, void_ptr_ty, size_ty, NULL);

  Function* spatial_store_check = (Function *) module.getOrInsertFunction("__softboundcets_spatial_store_dereference_check", void_ty, void_ptr_ty, void_ptr_ty, void_ptr_ty, size_ty, NULL);

  Function* temporal_load_check = (Function *) module.getOrInsertFunction("__softboundcets_temporal_load_dereference_check", void_ty, void_ptr_ty, size_ty, void_ptr_ty, void_ptr_ty, NULL);

  Function* temporal_store_check = (Function *)module.getOrInsertFunction("__softboundcets_temporal_store_dereference_check", void_ty, void_ptr_ty, size_ty, void_ptr_ty, void_ptr_ty, NULL);


  Function* global_init = (Function *) module.getOrInsertFunction("__softboundcets_global_init", void_ty, NULL);

  global_init->setDoesNotThrow();
  global_init->setLinkage(GlobalValue::InternalLinkage);

  BasicBlock* BB = BasicBlock::Create(module.getContext(), "entry", global_init);
  ReturnInst::Create(module.getContext(), BB);

}


bool InitializeSoftBound:: runOnModule (Module& module){
  
  constructCheckHandlers(module);
  constructShadowStackHandlers(module);
  constructMetadataHandlers(module); 
  //  constructAuxillaryFunctionHandlers(module);
}
