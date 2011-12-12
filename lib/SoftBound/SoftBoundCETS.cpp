//=== SoftBound/SoftBoundCETS.cpp - Pointer based Spatial and Temporal Memory Safety Pass --*- C++ -*===// 
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


#include "SoftBound/SoftBoundCETSPass.h"

static cl::opt<bool>
spatial_safety("softboundcets_spatial_safety",
               cl::desc("perform transformation for spatial safety"),
               cl::init(true));

static cl::opt<bool>
temporal_safety("softboundcets_temporal_safety",
                cl::desc("perform transformation for temporal safety"),
                cl::init(true));

static cl::opt<bool>
LOADCHECKS("softboundcets_spatial_safety_load_checks",
           cl::desc("introduce load dereference checks for spatial safety"),
           cl::init(true));

static cl::opt<bool>
STORECHECKS("softboundcets_spatial_safety_store_checks",
            cl::desc("introduce store dereference checks for spatial safety"),
            cl::init(true));

static cl::opt<bool>
TEMPORALLOADCHECKS("softboundcets_temporal_load_checks",
                   cl::desc("introduce temporal load dereference checks"),
                   cl::init(true));

static cl::opt<bool>
TEMPORALSTORECHECKS("softboundcets_temporal_store_checks",
                    cl::desc("introduce temporal store dereference checks"),
                    cl::init(true));

static cl::opt<bool>
FUNCDOMTEMPORALCHECKOPT("softboundcets_func_dom_temporal_check_opt",
                        cl::desc("eliminate redundant checks in the function using dominator based analysis"),
                        cl::init(true));


static cl::opt<bool>
STRUCTOPT("softboundcets_struct_opt", 
          cl::desc("enable or disable structure optimization"),
          cl::init(true));

static cl::opt<bool>
BOUNDSCHECKOPT ("softboundcets_bounds_check_opt",
                cl::desc("enable or disable dominator based load dereference check elimination"),
                cl::init(true));

static cl::opt<bool>
SHRINKBOUNDS ("softboundcets_shrink_bounds",
              cl::desc("enable shrinking bounds for the softboundboundcetswithss pass"),
              cl::init(false)); 

static cl::opt<bool>
MEMCOPYCHECK("softboundcets_memcopy_check",
             cl::desc("check memcopy calls"),
             cl::init(true));

static cl::opt<bool>
GLOBALCONSTANTOPT("softboundcets_global_const_opt",
                  cl::desc("global constant expressions are not checked"),
                  cl::init(true));

static cl::opt<bool>
CALLCHECKS("softboundcets_call_checks",
          cl::desc("introduce call checks"),
          cl::init(true));

static cl::opt<bool>
INDIRECTCALLCHECKS("softboundcets_indirect_call_checks",
                   cl::desc("introduce indirect call checks"),
                   cl::init(false));

static cl::opt<bool>
OPAQUECALLS("softboundcets_opaque_calls",
            cl::desc("consider all calls as opaque for func_dom_check_elimination"),
            cl::init(true));

static cl::opt<bool>
TEMPORALBOUNDSCHECKOPT("softboundcets_temporal_bounds_check_opt",
                       cl::desc("enable or disable temporal dominator based dereference check elimination"),
                       cl::init(true));

static cl::opt<bool>
STACKTEMPORALCHECKOPT("softboundcets_stack_temporal_check_opt",
                      cl::desc("eliminate temporal checks for stack variables"),
                      cl::init(true));

static cl::opt<bool>
GLOBALTEMPORALCHECKOPT("softboundcets_global_temporal_check_opt",
                       cl::desc("eliminate temporal checks for global variables"),
                       cl::init(true));

static cl::opt<bool>
BBDOMTEMPORALCHECKOPT("softboundcets_bb_dom_temporal_check_opt",
                      cl::desc("eliminate redundant checks in the basic block"),
                      cl::init(true));


char SoftBoundCETSPass:: ID = 0;

static RegisterPass<SoftBoundCETSPass> P ("SoftBoundCETSPass",
                                          "SoftBound Pass for Spatial Safety");


Value* SoftBoundCETSPass:: getAssociatedFuncLock(Value* pointer_inst){

  Instruction* inst = dyn_cast<Instruction>(pointer_inst);

  Value* tmp_lock = NULL;
  if(!inst){
    return NULL;
  }
  if(m_func_global_lock.count(inst->getParent()->getParent()->getName())){
    tmp_lock = m_func_global_lock[inst->getParent()->getParent()->getName()];
  }
  
  return tmp_lock;
}

void SoftBoundCETSPass::initializeSoftBoundVariables(Module& module) {

  /* Obtain the functions corresponding to dereference checks,
   * metadata retrieval and metadata store and various auxillary
   * functions 
   */

  m_spatial_load_dereference_check = module.getFunction("__softboundcets_spatial_load_dereference_check");
  assert(m_spatial_load_dereference_check && "__softboundcets_spatial_load_dereference_check function type null?");

  m_spatial_store_dereference_check = module.getFunction("__softboundcets_spatial_store_dereference_check");
  assert(m_spatial_store_dereference_check && "__softboundcets_spatial_store_dereference_check function type null?");

  m_temporal_load_dereference_check = module.getFunction("__softboundcets_temporal_load_dereference_check");
  assert(m_temporal_load_dereference_check && "__softboundcets_temporal_load_dereference_check function type null?");

  m_temporal_global_lock_function = module.getFunction("__softboundcets_get_global_lock");
  assert(m_temporal_global_lock_function && "__softboundcets_get_global_lock function type null?");

  m_temporal_store_dereference_check = module.getFunction("__softboundcets_temporal_store_dereference_check");
  assert(m_temporal_store_dereference_check && " __softboundcets_temporal_store_dereference_check function type null?");


  m_introspect_metadata = module.getFunction("__softboundcets_introspect_metadata");
  assert(m_introspect_metadata && "__softboundcets_introspect_metadata null?");
    
  m_copy_metadata = module.getFunction("__softboundcets_copy_metadata");
  assert(m_copy_metadata && "__softboundcets_copy_metadata NULL?");
    
  m_shadow_stack_allocate = module.getFunction("__softboundcets_allocate_shadow_stack_space");
  assert(m_shadow_stack_allocate && "__softboundcets_allocate_shadow_stack_space NULL?");

  m_shadow_stack_deallocate = module.getFunction("__softboundcets_deallocate_shadow_stack_space");
  assert(m_shadow_stack_deallocate && "__softboundcets_deallocate_shadow_stack_space NULL?");

  m_shadow_stack_base_load = module.getFunction("__softboundcets_load_base_shadow_stack");
  assert(m_shadow_stack_base_load && "__softboundcets_load_base_shadow_stack NULL?");

  m_shadow_stack_bound_load = module.getFunction("__softboundcets_load_bound_shadow_stack");
  assert(m_shadow_stack_bound_load && "__softboundcets_load_bound_shadow_stack NULL?");
    
  m_shadow_stack_key_load = module.getFunction("__softboundcets_load_key_shadow_stack");
  assert(m_shadow_stack_key_load && "__softboundcets_load_key_shadow_stack NULL?");
    
  m_shadow_stack_lock_load = module.getFunction("__softboundcets_load_lock_shadow_stack");
  assert(m_shadow_stack_lock_load && "__softboundcets_load_lock_shadow_stack NULL?");

  m_shadow_stack_base_store = module.getFunction("__softboundcets_store_base_shadow_stack");
  assert(m_shadow_stack_base_store && "__softboundcets_store_base_shadow_stack NULL?");

  m_shadow_stack_bound_store = module.getFunction("__softboundcets_store_bound_shadow_stack");
  assert(m_shadow_stack_bound_store && "__softboundcets_store_bound_shadow_stack NULL?");

  m_shadow_stack_key_store = module.getFunction("__softboundcets_store_key_shadow_stack");
  assert(m_shadow_stack_key_store && "__softboundcets_store_key_shadow_stack NULL?");

  m_shadow_stack_lock_store = module.getFunction("__softboundcets_store_lock_shadow_stack");
  assert(m_shadow_stack_lock_store && "__softboundcets_store_lock_shadow_stack NULL?");


  m_temporal_stack_memory_allocation = module.getFunction("__softboundcets_stack_memory_allocation");
  assert(m_temporal_stack_memory_allocation && "__softboundcets_stack_memory_allocation");

  m_temporal_stack_memory_deallocation = module.getFunction("__softboundcets_stack_memory_deallocation");
  assert(m_temporal_stack_memory_deallocation && "__softboundcets_stack_memory_deallocation not defined?");

  m_load_base_bound_func = module.getFunction("__softboundcets_metadata_load");
  assert(m_load_base_bound_func && "__softboundcets_metadata_load null?");

  m_store_base_bound_func = module.getFunction("__softboundcets_metadata_store");
  assert(m_store_base_bound_func && "__softboundcets_metadata_store null?");

  m_call_dereference_func = module.getFunction("__softboundcets_spatial_call_dereference_check");
  assert(m_call_dereference_func && "__softboundcets_spatial_call_dereference_check function null??");


  m_void_ptr_type = PointerType::getUnqual(Type::getInt8Ty(module.getContext()));
    
  size_t inf_bound;

  if(m_is_64_bit){
    m_key_type = Type::getInt64Ty(module.getContext());
  }
  else{
    m_key_type = Type::getInt32Ty(module.getContext());
  }

  if(m_is_64_bit) {
    inf_bound = (size_t) pow(2, 48);
  }
  else {
    inf_bound = (size_t) (2147483647);
  }
    
  ConstantInt* infinite_bound;

  if(m_is_64_bit) {
    infinite_bound = ConstantInt::get(Type::getInt64Ty(module.getContext()), inf_bound, false);
  }
  else {
    infinite_bound = ConstantInt::get(Type::getInt32Ty(module.getContext()), inf_bound, false);
  }
    
  m_infinite_bound_ptr = ConstantExpr::getIntToPtr(infinite_bound, 
                                                   m_void_ptr_type);
 
  PointerType* vptrty = dyn_cast<PointerType>(m_void_ptr_type);
  m_void_null_ptr = ConstantPointerNull::get(vptrty);
  
  PointerType* sizet_ptr_ty = NULL; 
  if(m_is_64_bit){
    sizet_ptr_ty = PointerType::getUnqual(Type::getInt64Ty(module.getContext()));
  }
  else{
    sizet_ptr_ty = PointerType::getUnqual(Type::getInt32Ty(module.getContext()));
  }

  m_sizet_null_ptr = ConstantPointerNull::get(sizet_ptr_ty);


  m_constantint32ty_one = ConstantInt::get(Type::getInt32Ty(module.getContext()), 1);
  m_constantint32ty_zero = ConstantInt::get(Type::getInt32Ty(module.getContext()), 0);

  m_constantint64ty_one = ConstantInt::get(Type::getInt64Ty(module.getContext()), 1);

  m_constantint64ty_zero = ConstantInt::get(Type::getInt64Ty(module.getContext()), 0);

  if(m_is_64_bit){
    m_constantint_one = m_constantint64ty_one;
    m_constantint_zero = m_constantint64ty_zero;
  }
  else{
    m_constantint_one = m_constantint32ty_one;
    m_constantint_zero = m_constantint32ty_zero;
  }
}

void SoftBoundCETSPass::getFunctionKeyLock(Function* func, Value* & func_key, Value* & func_lock, Value* & func_xmm_key_lock) {

  Function::iterator bb_begin = func->begin();
  BasicBlock* bb = dyn_cast<BasicBlock>(bb_begin);
  assert(bb && "Entry block does not exist?");
  Instruction* next_inst = NULL;
  Value* func_alloca_inst = NULL;

  if(!temporal_safety){
    func_key = NULL;
    func_lock = NULL;
    func_xmm_key_lock = NULL;    
    return;
  }

  /* iterate over the alloca instructions and then identify the introduceMemoryAllocationCall point */
  bool alloca_flag = false;
  for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i) {
    next_inst = dyn_cast<Instruction>(i);      
    Value* v1 = next_inst;
    if(isa<AllocaInst>(next_inst) && m_present_in_original.count(v1)){
      /* function has allocas */
      alloca_flag = true;
      func_alloca_inst = next_inst;
#if 0
      if(MEMTRACKER){          
        Instruction* insert_at = getNextInstruction(next_inst);
        addMemtrackerAllocation(func, next_inst, insert_at);
        func_cap = m_constantint64ty_zero;
        func_cap_addr = m_void_null_ptr;
      }
#endif
    }
  }

#if 0

  if(!MEMTRACKER){
#endif

    next_inst = NULL;
    for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i) {
      next_inst = dyn_cast<Instruction>(i);
        
      if(!isa<AllocaInst>(next_inst)){
        break;
      }
    }

    assert(next_inst != NULL && "basic block does not have instructions");
    if(alloca_flag){
      assert(func_alloca_inst && "alloca_inst_null?");
      Instruction* tmp_next_inst = dyn_cast<Instruction>(func_alloca_inst);
      next_inst = getNextInstruction(tmp_next_inst);
      /* first alloca in the function is passed as the address to the stack frame */
      addMemoryAllocationCall(func, func_alloca_inst, func_key, func_lock, next_inst, true);
    }

#if 0
  }    
#endif

  return;
}

void SoftBoundCETSPass::addMemoryAllocationCall(Function* func, Value* ptr, Value* & ptr_key, Value* & ptr_lock, Instruction* insert_at, bool is_stack) {

  SmallVector<Value*, 8> args;
  Instruction* first_inst_func = dyn_cast<Instruction>(func->begin()->begin());
  assert(first_inst_func && "function doesn't have any instruction and there is load???");

  AllocaInst* lock_alloca = new AllocaInst(m_void_ptr_type, "lock_alloca", first_inst_func);
  AllocaInst* key_alloca = new AllocaInst(Type::getInt64Ty(func->getContext()), "key_alloca", first_inst_func);

  Value* cast_ptr = castToVoidPtr(ptr, insert_at);

  args.push_back(cast_ptr);
  args.push_back(lock_alloca);
  args.push_back(key_alloca);
  
  CallInst::Create(m_temporal_stack_memory_allocation, args, "", insert_at);

  Instruction* alloca_lock = new LoadInst(lock_alloca, "lock.load", insert_at);
  Instruction* alloca_key = new LoadInst(key_alloca, "key.load", insert_at);

  ptr_key = alloca_key;
  ptr_lock = alloca_lock;
}




/* Renames the function main as pseudo_main */

void SoftBoundCETSPass::transformMain(Module& module) {
    
  Function* main_func = module.getFunction("main");

  /* if the program doesn't have main then don't do anything */
  if(!main_func) return;

  Type* ret_type = main_func->getReturnType();
  const FunctionType* fty = main_func->getFunctionType();
  std::vector<Type*> params;

  SmallVector<AttributeWithIndex, 8> param_attrs_vec;

  const AttrListPtr& pal = main_func->getAttributes();

  if(Attributes attrs = pal.getRetAttributes()) 
    param_attrs_vec.push_back(AttributeWithIndex::get(0, attrs));

    
  int arg_index = 1;

  for(Function::arg_iterator i = main_func->arg_begin(), e = main_func->arg_end();
      i != e; ++i, arg_index++) {

    params.push_back(i->getType());
    if(Attributes attrs = pal.getParamAttributes(arg_index))
      param_attrs_vec.push_back(AttributeWithIndex::get(params.size(), attrs));

  }

  FunctionType* nfty = FunctionType::get(ret_type, params, fty->isVarArg());
  Function* new_func = NULL;

  bool main_without_args = true;

  for(Function::arg_iterator fi = main_func->arg_begin(), fe = main_func->arg_end();
      fi != fe; ++fi) {
    main_without_args = false;
  }

  if(main_without_args) {
    new_func = Function::Create(nfty, main_func->getLinkage(), "softboundcets_pseudo_main");
  }
  else {
    new_func = Function::Create(nfty, main_func->getLinkage(), "pseudo_main");
  }

  new_func->copyAttributesFrom(main_func);
  new_func->setAttributes(AttrListPtr::get(param_attrs_vec.begin(), param_attrs_vec.end()));
    
  main_func->getParent()->getFunctionList().insert(main_func, new_func);
    
  SmallVector<Value*, 16> call_args;

  while(!main_func->use_empty()) {
      
    param_attrs_vec.clear();
    call_args.clear();

    CallSite cs(main_func->use_back());
    Instruction* call = cs.getInstruction();

    const AttrListPtr& call_pal = cs.getAttributes();

    if(!call)
      assert( 0 && "Non Call use of a function not handled");

      
    if (Attributes attrs = call_pal.getRetAttributes())
      param_attrs_vec.push_back(AttributeWithIndex::get(0, attrs));
      

    CallSite::arg_iterator arg_i = cs.arg_begin();
    arg_index = 1 ;

    for(Function::arg_iterator fi = main_func->arg_begin(), fe = main_func->arg_end();
        fi != fe; ++fi, ++arg_i, ++arg_index) {

      if(Attributes attrs = call_pal.getParamAttributes(arg_index))
        param_attrs_vec.push_back(AttributeWithIndex::get(call_args.size(), attrs));

      call_args.push_back(*arg_i);
    }

    Instruction* new_inst;

    new_inst = CallInst::Create(new_func, call_args, "", call);

    cast<CallInst>(new_inst)->setCallingConv(new_func->getCallingConv());
    cast<CallInst>(new_inst)->setAttributes(AttrListPtr::get(param_attrs_vec.begin(), 
                                                             param_attrs_vec.end()));
      
    call->eraseFromParent();
  }

  new_func->getBasicBlockList().splice(new_func->begin(), main_func->getBasicBlockList());

  Function::arg_iterator arg_i2 = new_func->arg_begin();

  for(Function::arg_iterator arg_i = main_func->arg_begin(), arg_e = main_func->arg_end(); 
      arg_i != arg_e; ++arg_i) {
      
    arg_i->replaceAllUsesWith(arg_i2);
    arg_i2->takeName(arg_i);

    ++arg_i2;
    arg_index++;
  }
                            
  main_func->eraseFromParent();
}

bool SoftBoundCETSPass::isFuncDefSoftBound(const std::string &str) {
  if (m_func_def_softbound.getNumItems() == 0) {

    m_func_wrappers_available["softboundcets__system"] = true;
    m_func_wrappers_available["softboundcets_setreuid"] = true;
    m_func_wrappers_available["softboundcets_mkstemp"] = true;
    m_func_wrappers_available["softboundcets_getuid"] = true;
    m_func_wrappers_available["softboundcets_getrlimit"] = true;
    m_func_wrappers_available["softboundcets_setrlimit"] = true;
    m_func_wrappers_available["softboundcets_fread"] = true;
    m_func_wrappers_available["softboundcets_umask"] = true;
    m_func_wrappers_available["softboundcets_mkdir"] = true;
    m_func_wrappers_available["softboundcets_chroot"] = true;
    m_func_wrappers_available["softboundcets_rmdir"] = true;
    m_func_wrappers_available["softboundcets_stat"] = true;
    m_func_wrappers_available["softboundcets_fputc"] = true;
    m_func_wrappers_available["softboundcets_fileno"] = true;
    m_func_wrappers_available["softboundcets_fgetc"] = true;
    m_func_wrappers_available["softboundcets_strncmp"] = true;
    m_func_wrappers_available["softboundcets_log"] = true;
    m_func_wrappers_available["softboundcets_fwrite"] = true;
    m_func_wrappers_available["softboundcets_atof"] = true;
    m_func_wrappers_available["softboundcets_feof"] = true;
    m_func_wrappers_available["softboundcets_remove"] = true;
    m_func_wrappers_available["softboundcets_acos"] = true;
    m_func_wrappers_available["softboundcets_atan2"] = true;
    m_func_wrappers_available["softboundcets_sqrtf"] = true;
    m_func_wrappers_available["softboundcets_expf"] = true;
    m_func_wrappers_available["softboundcets_exp2"] = true;
    m_func_wrappers_available["softboundcets_floorf"] = true;
    m_func_wrappers_available["softboundcets_ceil"] = true;
    m_func_wrappers_available["softboundcets_ceilf"] = true;
    m_func_wrappers_available["softboundcets_floor"] = true;
    m_func_wrappers_available["softboundcets_sqrt"] = true;
    m_func_wrappers_available["softboundcets_fabs"] = true;
    m_func_wrappers_available["softboundcets_abs"] = true;
    m_func_wrappers_available["softboundcets_srand"] = true;
    m_func_wrappers_available["softboundcets_srand48"] = true;
    m_func_wrappers_available["softboundcets_pow"] = true;
    m_func_wrappers_available["softboundcets_fabsf"] = true;
    m_func_wrappers_available["softboundcets_tan"] = true;
    m_func_wrappers_available["softboundcets_tanf"] = true;
    m_func_wrappers_available["softboundcets_tanl"] = true;
    m_func_wrappers_available["softboundcets_log10"] = true;
    m_func_wrappers_available["softboundcets_sin"] = true;
    m_func_wrappers_available["softboundcets_sinf"] = true;
    m_func_wrappers_available["softboundcets_sinl"] = true;
    m_func_wrappers_available["softboundcets_cos"] = true;
    m_func_wrappers_available["softboundcets_cosf"] = true;
    m_func_wrappers_available["softboundcets_cosl"] = true;
    m_func_wrappers_available["softboundcets_exp"] = true;
    m_func_wrappers_available["softboundcets_ldexp"] = true;
    m_func_wrappers_available["softboundcets_tmpfile"] = true;
    m_func_wrappers_available["softboundcets_ferror"] = true;
    m_func_wrappers_available["softboundcets_ftell"] = true;
    m_func_wrappers_available["softboundcets_fstat"] = true;
    m_func_wrappers_available["softboundcets_fflush"] = true;
    m_func_wrappers_available["softboundcets_fputs"] = true;
    m_func_wrappers_available["softboundcets_fopen"] = true;
    m_func_wrappers_available["softboundcets_fdopen"] = true;
    m_func_wrappers_available["softboundcets_fseek"] = true;
    m_func_wrappers_available["softboundcets_ftruncate"] = true;
    m_func_wrappers_available["softboundcets_popen"] = true;
    m_func_wrappers_available["softboundcets_fclose"] = true;
    m_func_wrappers_available["softboundcets_pclose"] = true;
    m_func_wrappers_available["softboundcets_rewind"] = true;
    m_func_wrappers_available["softboundcets_readdir"] = true;
    m_func_wrappers_available["softboundcets_opendir"] = true;
    m_func_wrappers_available["softboundcets_closedir"] = true;
    m_func_wrappers_available["softboundcets_rename"] = true;
    m_func_wrappers_available["softboundcets_sleep"] = true;
    m_func_wrappers_available["softboundcets_getcwd"] = true;
    m_func_wrappers_available["softboundcets_chown"] = true;
    m_func_wrappers_available["softboundcets_isatty"] = true;
    m_func_wrappers_available["softboundcets_chdir"] = true;
    m_func_wrappers_available["softboundcets_strcmp"] = true;
    m_func_wrappers_available["softboundcets_strcasecmp"] = true;
    m_func_wrappers_available["softboundcets_strncasecmp"] = true;
    m_func_wrappers_available["softboundcets_strlen"] = true;
    m_func_wrappers_available["softboundcets_strpbrk"] = true;
    m_func_wrappers_available["softboundcets_gets"] = true;
    m_func_wrappers_available["softboundcets_fgets"] = true;
    m_func_wrappers_available["softboundcets_perror"] = true;
    m_func_wrappers_available["softboundcets_strspn"] = true;
    m_func_wrappers_available["softboundcets_strcspn"] = true;
    m_func_wrappers_available["softboundcets_memcmp"] = true;
    m_func_wrappers_available["softboundcets_memchr"] = true;
    m_func_wrappers_available["softboundcets_rindex"] = true;
    m_func_wrappers_available["softboundcets_strtoul"] = true;
    m_func_wrappers_available["softboundcets_strtod"] = true;
    m_func_wrappers_available["softboundcets_strtol"] = true;
    m_func_wrappers_available["softboundcets_strchr"] = true;
    m_func_wrappers_available["softboundcets_strrchr"] = true;
    m_func_wrappers_available["softboundcets_strcpy"] = true;
    m_func_wrappers_available["softboundcets_abort"] = true;
    m_func_wrappers_available["softboundcets_rand"] = true;
    m_func_wrappers_available["softboundcets_atoi"] = true;
    m_func_wrappers_available["softboundcets_puts"] = true;
    m_func_wrappers_available["softboundcets_exit"] = true;
    m_func_wrappers_available["softboundcets_strtok"] = true;
    m_func_wrappers_available["softboundcets_strdup"] = true;
    m_func_wrappers_available["softboundcets_strcat"] = true;
    m_func_wrappers_available["softboundcets_strncat"] = true;
    m_func_wrappers_available["softboundcets_strncpy"] = true;
    m_func_wrappers_available["softboundcets_strstr"] = true;
    m_func_wrappers_available["softboundcets_signal"] = true;
    m_func_wrappers_available["softboundcets_clock"] = true;
    m_func_wrappers_available["softboundcets_atol"] = true;
    m_func_wrappers_available["softboundcets_realloc"] = true;
    m_func_wrappers_available["softboundcets_calloc"] = true;
    m_func_wrappers_available["softboundcets_malloc"] = true;
    m_func_wrappers_available["softboundcets_putchar"] = true;
    m_func_wrappers_available["softboundcets_times"] = true;
    m_func_wrappers_available["softboundcets_strftime"] = true;
    m_func_wrappers_available["softboundcets_localtime"] = true;
    m_func_wrappers_available["softboundcets_time"] = true;
    m_func_wrappers_available["softboundcets_drand48"] = true;
    m_func_wrappers_available["softboundcets_free"] = true;
    m_func_wrappers_available["softboundcets_lrand48"] = true;
    m_func_wrappers_available["softboundcets_ctime"] = true;
    m_func_wrappers_available["softboundcets_difftime"] = true;
    m_func_wrappers_available["softboundcets_toupper"] = true;
    m_func_wrappers_available["softboundcets_tolower"] = true;
    m_func_wrappers_available["softboundcets_setbuf"] = true;
    m_func_wrappers_available["softboundcets_getenv"] = true;
    m_func_wrappers_available["softboundcets_atexit"] = true;
    m_func_wrappers_available["softboundcets_strerror"] = true;
    m_func_wrappers_available["softboundcets_unlink"] = true;
    m_func_wrappers_available["softboundcets_close"] = true;
    m_func_wrappers_available["softboundcets_open"] = true;
    m_func_wrappers_available["softboundcets_read"] = true;
    m_func_wrappers_available["softboundcets_write"] = true;
    m_func_wrappers_available["softboundcets_lseek"] = true;
    m_func_wrappers_available["softboundcets_gettimeofday"] = true;
    m_func_wrappers_available["softboundcets_select"] = true;
    m_func_wrappers_available["softboundcets___errno_location"] = true;
    m_func_wrappers_available["softboundcets___ctype_b_loc"] = true;
    m_func_wrappers_available["softboundcets___ctype_toupper_loc"] = true;
    m_func_wrappers_available["softboundcets___ctype_tolower_loc"] = true;
    m_func_wrappers_available["softboundcets_qsort"] = true;





    
    
    
    m_func_def_softbound["__softboundcets_introspect_metadata"] = true;
    m_func_def_softbound["__softboundcets_copy_metadata"] = true;
    m_func_def_softbound["__softboundcets_allocate_shadow_stack_space"] = true;
    m_func_def_softbound["__softboundcets_load_base_shadow_stack"] = true;
    m_func_def_softbound["__softboundcets_load_bound_shadow_stack"] = true;
    m_func_def_softbound["__softboundcets_load_key_shadow_stack"] = true;
    m_func_def_softbound["__softboundcets_load_lock_shadow_stack"] = true;
    m_func_def_softbound["__softboundcets_store_base_shadow_stack"] = true;      
    m_func_def_softbound["__softboundcets_store_bound_shadow_stack"] = true;      
    m_func_def_softbound["__softboundcets_store_key_shadow_stack"] = true;      
    m_func_def_softbound["__softboundcets_store_lock_shadow_stack"] = true;      
    m_func_def_softbound["__softboundcets_deallocate_shadow_stack_space"] = true;

    m_func_def_softbound["__softboundcets_trie_allocate"] = true;
    m_func_def_softbound["__shrinkBounds"] = true;
    m_func_def_softbound["__softboundcets_spatial_load_dereference_check"] = true;
    m_func_def_softbound["__softboundcets_spatial_store_dereference_check"] = true;
    m_func_def_softbound["__softboundcets_spatial_call_dereference_check"] = true;
    m_func_def_softbound["__softboundcets_temporal_load_dereference_check"] = true;
    m_func_def_softbound["__softboundcets_temporal_store_dereference_check"] = true;
    m_func_def_softbound["__softboundcets_stack_memory_allocation"] = true;
    m_func_def_softbound["__softboundcets_memory_allocation"] = true;
    m_func_def_softbound["__softboundcets_get_global_lock"] = true;
    m_func_def_softbound["__softboundcets_add_to_free_map"] = true;
    m_func_def_softbound["__softboundcets_check_remove_from_free_map"] = true;
    m_func_def_softbound["__softboundcets_allocation_secondary_trie_allocate"] = true;
    m_func_def_softbound["__softboundcets_allocation_secondary_trie_allocate_range"] = true;
    m_func_def_softbound["__softboundcets_allocate_lock_location"] = true;
    m_func_def_softbound["__softboundcets_memory_deallocation"] = true;
    m_func_def_softbound["__softboundcets_stack_memory_deallocation"] = true;

    

    m_func_def_softbound["__softboundcets_metadata_load"] = true;
    m_func_def_softbound["__softboundcets_metadata_store"] = true;
    m_func_def_softbound["__hashProbeAddrOfPtr"] = true;
    m_func_def_softbound["__memcopyCheck"] = true;
    m_func_def_softbound["__memcopyCheck_i64"] = true;

    m_func_def_softbound["__softboundcets_global_init"] = true;      
    m_func_def_softbound["__softboundcets_init"] = true;      
    m_func_def_softbound["__softboundcets_abort"] = true;      
    m_func_def_softbound["__softboundcets_printf"] = true;
    
    m_func_def_softbound["__softboundcets_stub"] = true;

    m_func_def_softbound["safe_calloc"] = true;
    m_func_def_softbound["safe_malloc"] = true;
    m_func_def_softbound["safe_free"] = true;

    m_func_def_softbound["__assert_fail"] = true;
    m_func_def_softbound["assert"] = true;
    m_func_def_softbound["__strspn_c2"] = true;
    m_func_def_softbound["__strcspn_c2"] = true;
    m_func_def_softbound["__strtol_internal"] = true;
    m_func_def_softbound["__strtod_internal"] = true;
    m_func_def_softbound["_IO_getc"] = true;
    m_func_def_softbound["_IO_putc"] = true;
    m_func_def_softbound["__xstat"] = true;

    m_func_def_softbound["select"] = true;
    m_func_def_softbound["_setjmp"] = true;
    m_func_def_softbound["longjmp"] = true;
    m_func_def_softbound["fork"] = true;
    m_func_def_softbound["pipe"] = true;
    m_func_def_softbound["dup2"] = true;
    m_func_def_softbound["execv"] = true;
    m_func_def_softbound["compare_pic_by_pic_num_desc"] = true;
     
    m_func_def_softbound["wprintf"] = true;
    m_func_def_softbound["vfprintf"] = true;
    m_func_def_softbound["vsprintf"] = true;
    m_func_def_softbound["fprintf"] = true;
    m_func_def_softbound["printf"] = true;
    m_func_def_softbound["sprintf"] = true;
    m_func_def_softbound["snprintf"] = true;

    m_func_def_softbound["scanf"] = true;
    m_func_def_softbound["fscanf"] = true;
    m_func_def_softbound["sscanf"] = true;   
  }

  // Is the function name in the above list?
  if (m_func_def_softbound.count(str) > 0) {
    return true;
  }

  /* handling new intrinsics which have isoc99 in their name */
  if(str.find("isoc99") != std::string::npos){
    return true;
  }

  // If the function is an llvm intrinsic, don't transform it
  if (str.find("llvm.") == 0) {
    return true;
  }

  return false;
}

void SoftBoundCETSPass::identifyFuncToTrans(Module& module) {
    
  for(Module::iterator fb_it = module.begin(), fe_it = module.end(); 
      fb_it != fe_it; ++fb_it) {

    Function* func = dyn_cast<Function>(fb_it);
    assert(func && " Not a function");

    /* Check if the function is defined in the module */
    if(!func->isDeclaration()) {
      if(isFuncDefSoftBound(func->getName())) 
        continue;
      
      m_func_softboundcets_transform[func->getName()] = true;
      if(hasPtrArgRetType(func)) {
        m_func_to_transform[func->getName()] = true;
      }
    }
  }
}

Value* SoftBoundCETSPass::introduceGlobalLockFunction(Instruction* insert_at){

  SmallVector<Value*, 8> args;
  Value* call_inst = CallInst::Create(m_temporal_global_lock_function, args, "", insert_at);
  return call_inst;
}


Value* SoftBoundCETSPass:: castToVoidPtr(Value* operand, Instruction* insert_at) {

  Value* cast_bitcast = operand;
  if(operand->getType() != m_void_ptr_type) {
    cast_bitcast = new BitCastInst(operand, m_void_ptr_type,
                                   "bitcast",
                                   insert_at);
  }
  return cast_bitcast;

}

/* Check if the function has either pointer arguments or returns a
 * pointer. This function is used for ascertaining whether the
 * function needs to transformed to allow base or bound propagation
 * or not
 */
bool SoftBoundCETSPass::hasPtrArgRetType(Function* func) {
   
  const Type* ret_type = func->getReturnType();
  if(isa<PointerType>(ret_type))
    return true;

  for(Function::arg_iterator i = func->arg_begin(), e = func->arg_end(); 
      i != e; ++i) {
      
    if(isa<PointerType>(i->getType()))
      return true;
  }
  return false;
}

/* getNextInstruction : Get the next instruction after the
 * instruction provided as the argument. Assert needed by the caller
 * of this func to check if it is NULL
 */
Instruction* SoftBoundCETSPass::getNextInstruction(Instruction* inst) {

  BasicBlock* basic_block = inst->getParent();
  for(BasicBlock::iterator ib = basic_block->begin(), ie = basic_block->end(); 
      ib != ie; ++ib) {
    Instruction* current = dyn_cast<Instruction>(ib);
    if(current == inst) {
      Instruction* ret_inst = dyn_cast<Instruction>(++ib);
      if(ret_inst) {
        return ret_inst;
      }
      else {
        return basic_block->getTerminator();
      }
    }
  }
  return NULL;
}

void SoftBoundCETSPass::addStoreBaseBoundFunc(Value* pointer_dest, 
                                              Value* pointer_base, 
                                              Value* pointer_bound, 
                                              Value* pointer_key,
                                              Value* pointer_lock,
                                              Value* pointer,
                                              Value* size_of_type, 
                                              Instruction* insert_at) {

  Value* pointer_base_cast = NULL;
  Value* pointer_bound_cast = NULL;

  
  Value* pointer_dest_cast = castToVoidPtr(pointer_dest, insert_at);

  if(spatial_safety){
    pointer_base_cast = castToVoidPtr(pointer_base, insert_at);
    pointer_bound_cast = castToVoidPtr(pointer_bound, insert_at);
  }
  //  Value* pointer_cast = castToVoidPtr(pointer, insert_at);
    
  SmallVector<Value*, 8> args;

  args.push_back(pointer_dest_cast);

  if(spatial_safety){
    args.push_back(pointer_base_cast);
    args.push_back(pointer_bound_cast);
  }

  if(temporal_safety){
    args.push_back(pointer_key);
    args.push_back(pointer_lock);
  }
  CallInst::Create(m_store_base_bound_func, args, "", insert_at);
}


void SoftBoundCETSPass::handlePHIPass2(PHINode* phi_node) {


  /* We are concerned only with phi nodes which are pointers */
  if(!isa<PointerType>(phi_node->getType())) 
    return;

  PHINode* base_phi_node = NULL;
  PHINode* bound_phi_node  = NULL;
  PHINode* key_phi_node = NULL;
  PHINode* lock_phi_node = NULL;
  
  if(spatial_safety){
    base_phi_node = dyn_cast<PHINode>(getAssociatedBase(phi_node));
    bound_phi_node = dyn_cast<PHINode>(getAssociatedBound(phi_node));
  }

  if(temporal_safety){
    key_phi_node = dyn_cast<PHINode>(getAssociatedKey(phi_node));
    Value* func_lock = getAssociatedFuncLock(phi_node);
    lock_phi_node= dyn_cast<PHINode>(getAssociatedLock(phi_node, func_lock));
  }
  
  std::map<Value*, Value*> globals_base;
  std::map<Value*, Value*> globals_bound;

  std::map<Value*, Value*> globals_key;
  std::map<Value*, Value*> globals_lock;
 
  unsigned num_incoming_values = phi_node->getNumIncomingValues();

  for(unsigned m = 0; m < num_incoming_values; m++) {

    Value* incoming_value = phi_node->getIncomingValue(m);
    BasicBlock* bb_incoming = phi_node->getIncomingBlock(m);

    if(isa<ConstantPointerNull>(incoming_value)) {
      if(spatial_safety){
        base_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
        bound_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
      }
      if(temporal_safety){
        key_phi_node->addIncoming(m_constantint64ty_zero, bb_incoming);
        lock_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
      }
      continue;
    } /* ConstantPointerNull ends */
    
        
    /* It is possible that the phi node can have undef values!!! */
    if (isa<UndefValue>(incoming_value)) {        
      //      UndefValue* undef_value = UndefValue::get(m_void_ptr_type);
      if(spatial_safety){
        base_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
        bound_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
      }
      if(temporal_safety){
        key_phi_node->addIncoming(m_constantint64ty_zero, bb_incoming);
        lock_phi_node->addIncoming(m_void_null_ptr, bb_incoming);
      }      
      continue;
    } /* Undef value ends */
      
    Value* incoming_value_base = NULL;
    Value* incoming_value_bound = NULL;
    Value* incoming_value_key  = NULL;
    Value* incoming_value_lock = NULL;
    
    GlobalVariable* gv = dyn_cast<GlobalVariable>(incoming_value);
      
    /* handle global variables */
      
    if(gv) {

      if(spatial_safety){
        if(!globals_base.count(gv)){
          Value* tmp_base = NULL;
          Value* tmp_bound = NULL;
          getGlobalVariableBaseBound(incoming_value, tmp_base, tmp_bound);
          assert(tmp_base && "base of a global variable null?");
          assert(tmp_bound && "bound of a global variable null?");
          
          incoming_value_base = castToVoidPtr(tmp_base, phi_node->getParent()->getParent()->begin()->begin());
          incoming_value_bound = castToVoidPtr(tmp_bound, phi_node->getParent()->getParent()->begin()->begin());
            
          globals_base[incoming_value] = incoming_value_base;
          globals_bound[incoming_value] = incoming_value_bound;       
        }
        else {
          incoming_value_base = globals_base[incoming_value];
          incoming_value_bound = globals_bound[incoming_value];          
        }
      } /* spatial safety ends */
      
      if(temporal_safety){
        incoming_value_key = m_constantint64ty_one;
        Value* tmp_lock = m_func_global_lock[phi_node->getParent()->getParent()->getName()];
        incoming_value_lock = tmp_lock;
      }
    }/* Global Variable ends */
      
    /* handle constant expressions */
    Constant* given_constant = dyn_cast<Constant>(incoming_value);
    if(given_constant){

      if(spatial_safety){
        if(!globals_base.count(incoming_value)){
          Value* tmp_base = NULL;
          Value* tmp_bound = NULL;
          getConstantExprBaseBound(given_constant, tmp_base, tmp_bound);
          assert(tmp_base && tmp_bound  &&"[handlePHIPass2] tmp_base tmp_bound, null?");
          incoming_value_base = castToVoidPtr(tmp_base, phi_node->getParent()->getParent()->begin()->begin());
          incoming_value_bound = castToVoidPtr(tmp_bound, phi_node->getParent()->getParent()->begin()->begin());
          
          globals_base[incoming_value] = incoming_value_base;
          globals_bound[incoming_value] = incoming_value_bound;        
        }
        else{
          incoming_value_base = globals_base[incoming_value];
          incoming_value_bound = globals_bound[incoming_value];          
        }
      }/* spatial safety ends */

      if(temporal_safety){
        incoming_value_key = m_constantint64ty_one;
        Value* tmp_lock = m_func_global_lock[phi_node->getParent()->getParent()->getName()];
        incoming_value_lock = tmp_lock;
      }
    }

    /* handle values having map based pointer base and bounds */     
    if(spatial_safety && checkBaseBoundMetadataPresent(incoming_value)){
      incoming_value_base = getAssociatedBase(incoming_value);
      incoming_value_bound = getAssociatedBound(incoming_value);
    }

    if(temporal_safety && checkKeyLockMetadataPresent(incoming_value)){
      incoming_value_key = getAssociatedKey(incoming_value);
      Value* func_lock = getAssociatedFuncLock(phi_node);
      incoming_value_lock = getAssociatedLock(incoming_value, func_lock);
    }
    
    if(spatial_safety){
      assert(incoming_value_base && "[handlePHIPass2] incoming_value doesn't have base?");
      assert(incoming_value_bound && "[handlePHIPass2] incoming_value doesn't have bound?");
      
      base_phi_node->addIncoming(incoming_value_base, bb_incoming);
      bound_phi_node->addIncoming(incoming_value_bound, bb_incoming);
    }

    if(temporal_safety){
      assert(incoming_value_key && "[handlePHIPass2] incoming_value doesn't have key?");
      assert(incoming_value_lock && "[handlePHIPass2] incoming_value doesn't have lock?");

      key_phi_node->addIncoming(incoming_value_key, bb_incoming);
      lock_phi_node->addIncoming(incoming_value_lock, bb_incoming);
    }
    
      
  } /* iterating over incoming values ends */

  if(spatial_safety){
    assert(base_phi_node && "[handlePHIPass2] base_phi_node null?");
    assert(bound_phi_node && "[handlePHIPass2] bound_phi_node null?");
  }
  
  if(temporal_safety){
    assert(key_phi_node && "[handlePHIPass2] key_phi_node null?");
    assert(lock_phi_node && "[handlePHIPass2] lock_phi_node null?");
  }
  
  unsigned n_values = phi_node->getNumIncomingValues();
  if(spatial_safety){
    unsigned n_base_values = base_phi_node->getNumIncomingValues();
    unsigned n_bound_values = bound_phi_node->getNumIncomingValues();    
    assert((n_values == n_base_values)  && "[handlePHIPass2] number of values different for original phi node and the base phi node");
    assert((n_values == n_bound_values) && "[handlePHIPass2] number of values different for original phi node and the bound phi node");
  }
  
  if(temporal_safety){
    unsigned n_key_values = key_phi_node->getNumIncomingValues();
    unsigned n_lock_values = lock_phi_node->getNumIncomingValues();
    assert((n_values == n_key_values)  && "[handlePHIPass2] number of values different for original phi node and the key phi node");
    assert((n_values == n_lock_values) && "[handlePHIPass2] number of values different for original phi node and the lock phi node");
  }  
}

void SoftBoundCETSPass:: propagateMetadata(Value* pointer_operand, Instruction* inst, int instruction_type){

  /*  Need to just propagate the base and bound here if I am not
   *  shrinking bounds
   */

  if(spatial_safety){

    if(checkBaseBoundMetadataPresent(inst)){
      /* Base-Bound introduced in the first pass */      
      return;
    }
  }
  if(temporal_safety){
    
    if(checkKeyLockMetadataPresent(inst)){
      /* Key/Lock introduced in the first pass */
      return;
    }    
  }

  if(isa<ConstantPointerNull>(pointer_operand)) {
    if(spatial_safety){
      associateBaseBound(inst, m_void_null_ptr, m_void_null_ptr);
    }
    if(temporal_safety){
      associateKeyLock(inst, m_constantint64ty_zero, m_void_null_ptr);
    }
    return;
  }

  if(spatial_safety){
    
    if(checkBaseBoundMetadataPresent(pointer_operand)){
      
      Value* tmp_base = getAssociatedBase(pointer_operand); 
      Value* tmp_bound = getAssociatedBound(pointer_operand);       
      associateBaseBound(inst, tmp_base, tmp_bound);
    }
    else{
      if(isa<Constant>(pointer_operand)) {
        
        Value* tmp_base = NULL;
        Value* tmp_bound = NULL;
        Constant* given_constant = dyn_cast<Constant>(pointer_operand);
        getConstantExprBaseBound(given_constant, tmp_base, tmp_bound);
        assert(tmp_base && "gep with cexpr and base null?");
        assert(tmp_bound && "gep with cexpr and bound null?");
        tmp_base = castToVoidPtr(tmp_base, inst);
        tmp_bound = castToVoidPtr(tmp_bound, inst);        
    
        associateBaseBound(inst, tmp_base, tmp_bound);
      }/* constant check ends here */
      /* could be in the first pass, do nothing here */
    }
  } /* spatial safety ends here */

  if(temporal_safety){
    if(checkKeyLockMetadataPresent(pointer_operand)){      
      Value* tmp_key = getAssociatedKey(pointer_operand);
      Value* func_lock = getAssociatedFuncLock(inst);
      Value* tmp_lock = getAssociatedLock(pointer_operand, func_lock);
      associateKeyLock(inst, tmp_key, tmp_lock);
    }
    else{      
      if(isa<Constant>(pointer_operand)){
        Value* func_lock = m_func_global_lock[inst->getParent()->getParent()->getName()];
        associateKeyLock(inst, m_constantint64ty_one, func_lock);
      }
    }
  } /* temporal safety ends here */

}

void SoftBoundCETSPass::handleBitCast(BitCastInst* bitcast_inst) {

  Value* pointer_operand = bitcast_inst->getOperand(0);
  
  propagateMetadata(pointer_operand, bitcast_inst, SBCETS_BITCAST);

#if 0   
  if(isa<ConstantPointerNull>(pointer_operand)) {
    associateBaseBound(pointer_operand, m_void_null_ptr, m_void_null_ptr);
    return;
  }
   
  if(checkBaseBoundMetadataPresent(bitcast_inst)){
    /* Base-Bound introduced in the first pass */
    return;
  }

  if(checkBaseBoundMetadataPresent(pointer_operand)){
      
    Value* tmp_base = getAssociatedBase(pointer_operand); 
    Value* tmp_cast_base = NULL;
        
    /* FIX : If I don't cast them here, phi nodes will complain. This is
     * because phi nodes can have multiple entries for the same
     * incoming value and then, base and bound for such an incoming
     * value will be different and hence breaks LLVM assertion
     */      
    tmp_cast_base = castToVoidPtr(tmp_base, bitcast_inst);    
    Value* tmp_bound = getAssociatedBound(pointer_operand); 
    Value* tmp_cast_bound = NULL;
    tmp_cast_bound = castToVoidPtr(tmp_bound, bitcast_inst);
    associateBaseBound(bitcast_inst, tmp_cast_base, tmp_cast_bound);
  }
#endif

}

void SoftBoundCETSPass::getGlobalVariableBaseBound(Value* operand, Value* & operand_base, Value* & operand_bound){

  GlobalVariable* gv = dyn_cast<GlobalVariable>(operand);
  Module* module = gv->getParent();
  assert(gv && "[getGlobalVariableBaseBound] not a global variable?");
    
  // here implies the global was initially present before the transformation

  std::vector<Constant*> indices_base;
  Constant* index_base = ConstantInt::get(Type::getInt32Ty(module->getContext()), 0);
  indices_base.push_back(index_base);

  Constant* base_exp = ConstantExpr::getGetElementPtr(gv, indices_base);
        
  std::vector<Constant*> indices_bound;
  Constant* index_bound = ConstantInt::get(Type::getInt32Ty(module->getContext()), 1);
  indices_bound.push_back(index_bound);
    
  Constant* bound_exp = ConstantExpr::getGetElementPtr(gv, indices_bound);
    
  operand_base = base_exp;
  operand_bound = bound_exp;


    
}


void SoftBoundCETSPass::handlePHIPass1(PHINode* phi_node) {

  /* If the phi_node returns a pointer then insert phi-nodes if the
   * base and bound of the pointers involved in the phi_node are
   * available
   */
  if(!isa<PointerType>(phi_node->getType()))
    return;


  unsigned num_incoming_values = phi_node->getNumIncomingValues();

  if(spatial_safety){
    PHINode* base_phi_node = PHINode::Create(m_void_ptr_type,
                                             num_incoming_values,
                                             "phi.base",
                                             phi_node);
    
    PHINode* bound_phi_node = PHINode::Create(m_void_ptr_type, 
                                              num_incoming_values,
                                              "phi.bound", 
                                              phi_node);
    
    Value* base_phi_node_value = base_phi_node;
    Value* bound_phi_node_value = bound_phi_node;
  
    associateBaseBound(phi_node, base_phi_node_value, bound_phi_node_value);
  }

  if(temporal_safety){
    PHINode* key_phi_node = PHINode::Create(Type::getInt64Ty(phi_node->getType()->getContext()),
                                            num_incoming_values,
                                            "phi.key", phi_node);

    PHINode* lock_phi_node = PHINode::Create(m_void_ptr_type, 
                                             num_incoming_values,
                                             "phi.lock", phi_node);
    
    associateKeyLock(phi_node, key_phi_node, lock_phi_node);
  }

}

void SoftBoundCETSPass:: introduceShadowStackAllocation(CallInst* call_inst){
    
  /* count the number of pointer arguments and whether a pointer return */
    
  int pointer_args_return = getNumPointerArgsAndReturn(call_inst);

  if(pointer_args_return == 0)
    return;

  Value* total_ptr_args;    
  total_ptr_args = ConstantInt::get(Type::getInt32Ty(call_inst->getType()->getContext()), pointer_args_return, false);

  SmallVector<Value*, 8> args;
  args.push_back(total_ptr_args);
  CallInst::Create(m_shadow_stack_allocate, args, "", call_inst);
}


void SoftBoundCETSPass::introduceShadowStackStores(Value* ptr_value, Instruction* insert_at, int arg_no){
  if(!isa<PointerType>(ptr_value->getType())){
    return;
  }

  Value* argno_value;    
  argno_value = ConstantInt::get(Type::getInt32Ty(ptr_value->getType()->getContext()), arg_no, false);


  if(spatial_safety){

    Value* ptr_base = getAssociatedBase(ptr_value);
    Value* ptr_bound = getAssociatedBound(ptr_value);
    
    Value* ptr_base_cast = castToVoidPtr(ptr_base, insert_at);
    Value* ptr_bound_cast = castToVoidPtr(ptr_bound, insert_at);

    SmallVector<Value*, 8> args;
    args.push_back(ptr_base_cast);
    args.push_back(argno_value);
    CallInst::Create(m_shadow_stack_base_store, args, "", insert_at);
    
    args.clear();
    args.push_back(ptr_bound_cast);
    args.push_back(argno_value);
    CallInst::Create(m_shadow_stack_bound_store, args, "", insert_at);    
  }

  if(temporal_safety){

    Value* ptr_key = getAssociatedKey(ptr_value);    
    Value* func_lock = getAssociatedFuncLock(insert_at);
    Value* ptr_lock = getAssociatedLock(ptr_value, func_lock);

    
    SmallVector<Value*, 8> args;
    args.clear();
    args.push_back(ptr_key);
    args.push_back(argno_value);
    CallInst::Create(m_shadow_stack_key_store, args, "", insert_at);

    args.clear();
    args.push_back(ptr_lock);
    args.push_back(argno_value);
    CallInst::Create(m_shadow_stack_lock_store, args, "", insert_at);
  }
    
}

  

void SoftBoundCETSPass:: introduceShadowStackDeallocation(CallInst* call_inst, Instruction* insert_at){

  int pointer_args_return = getNumPointerArgsAndReturn(call_inst);

  if(pointer_args_return == 0)
    return;

  SmallVector<Value*, 8> args;    
  CallInst::Create(m_shadow_stack_deallocate, args, "", insert_at);
}

int SoftBoundCETSPass:: getNumPointerArgsAndReturn(CallInst* call_inst){

  int total_pointer_count = 0;
  SmallVector<AttributeWithIndex, 8> param_attrs_vec;
  call_inst->setAttributes(AttrListPtr::get(param_attrs_vec.begin(), param_attrs_vec.end()));  
    
  CallSite cs(call_inst);
  for(unsigned i = 0; i < cs.arg_size(); i++){
    Value* arg_value = cs.getArgument(i);
    if(isa<PointerType>(arg_value->getType())){
      total_pointer_count++;
    }
  }

  if(total_pointer_count != 0){
    /* reserve one for the return address if it has atleast one pointer argument */
    total_pointer_count++;
  }
  else{

    /* increment the pointer arg return if the call instruction returns a pointer */
    if(isa<PointerType>(call_inst->getType())){
      total_pointer_count++;
    }
  }


  return total_pointer_count;
}


void SoftBoundCETSPass::introduceShadowStackLoads(Value* ptr_value, Instruction* insert_at, int arg_no){

    
  if(!isa<PointerType>(ptr_value->getType())){
    return;
  }
    
  Value* argno_value;    
  argno_value = ConstantInt::get(Type::getInt32Ty(ptr_value->getType()->getContext()), arg_no, false);
    
  SmallVector<Value*, 8> args;

  if(spatial_safety){
    args.clear();
    args.push_back(argno_value);
    Value* base = CallInst::Create(m_shadow_stack_base_load, args, "", insert_at);
    
    args.clear();
    args.push_back(argno_value);
    Value* bound = CallInst::Create(m_shadow_stack_bound_load, args, "", insert_at);
    
    associateBaseBound(ptr_value, base, bound);
  }
  
  if(temporal_safety){
    args.clear();
    args.push_back(argno_value);
    Value* key = CallInst::Create(m_shadow_stack_key_load, args, "", insert_at);

    args.clear();
    args.push_back(argno_value);
    Value* lock = CallInst::Create(m_shadow_stack_lock_load, args, "", insert_at);

    associateKeyLock(ptr_value, key, lock);

  }
    
}

void SoftBoundCETSPass:: dissociateKeyLock(Value* pointer_operand){

    if(m_pointer_key.count(pointer_operand)){
      m_pointer_key.erase(pointer_operand);
    }

    if(m_pointer_lock.count(pointer_operand)){
      m_pointer_lock.erase(pointer_operand);
    }

    assert((m_pointer_key.count(pointer_operand) == 0) && "dissociating key failed");    
    assert((m_pointer_lock.count(pointer_operand) == 0) && "dissociating lock failed");


}



/* Removes the base, bound, key and lock with the pointer */

void SoftBoundCETSPass::dissociateBaseBound(Value* pointer_operand){

  if(m_pointer_base.count(pointer_operand)){
    m_pointer_base.erase(pointer_operand);
  }

  if(m_pointer_bound.count(pointer_operand)){
    m_pointer_bound.erase(pointer_operand);
  }

  assert((m_pointer_base.count(pointer_operand) == 0) && "dissociating base failed\n");
  assert((m_pointer_bound.count(pointer_operand) == 0) && "dissociating bound failed");
}

void SoftBoundCETSPass::associateKeyLock(Value* pointer_operand, Value* pointer_key, Value* pointer_lock){
  
  if(m_pointer_key.count(pointer_operand)){
    dissociateKeyLock(pointer_operand);
  }
  
  if(pointer_key->getType() != m_key_type){
    assert(0 && "key does not the right type ");
  }

  if(pointer_lock->getType() != m_void_ptr_type){
    assert(0 && "lock does not have the right type");
  }

  m_pointer_key[pointer_operand] = pointer_key;

  if(m_pointer_lock.count(pointer_operand)){
    assert(0 && "lock already has an entry in the map");
  }
  m_pointer_lock[pointer_operand] = pointer_lock;
   
}

void SoftBoundCETSPass::associateBaseBound(Value* pointer_operand, Value* pointer_base, Value* pointer_bound){

  if(m_pointer_base.count(pointer_operand)){
    /* do something if it already exists in the map */
    dissociateBaseBound(pointer_operand);
    //      assert(0 && "base map already has an entry in the map");
  }

  if(pointer_base->getType() != m_void_ptr_type){
    assert(0 && "base does not have a void pointer type ");
  }
  m_pointer_base[pointer_operand] = pointer_base;
    

  if(m_pointer_bound.count(pointer_operand)){
    assert(0 && "bound map already has an entry in the map");
  }

  if(pointer_bound->getType() != m_void_ptr_type) {
    assert(0 && "bound does not have a void pointer type ");
  }
  m_pointer_bound[pointer_operand] = pointer_bound;

}


/* Base and Bound which are inputs to the phi node may not be of i8*
 * type, so this functions inserts a bitcast instructions and then
 * adds to the phi_node. A PHINode must always be grouped at the top
 * of the basic block
 */
void SoftBoundCETSPass::castAddToPhiNode(PHINode* phi_node, Value* base_bound, 
                                               BasicBlock* bb_incoming, 
                                               std::map<Value*, Value*> & base_bound_map, 
                                               Value* map_index) {
    
  if( base_bound->getType() != phi_node->getType()) {
      
    if(base_bound_map.count(base_bound)) {
      assert(0 && "already base bound cast exists for incoming value and I am still casting it ???");
    }

    const PointerType* func_ptr_type = dyn_cast<PointerType>(base_bound->getType());

    if(func_ptr_type) {
      if(isa<FunctionType>(func_ptr_type->getElementType())) {
          
        BitCastInst* incoming_tmp_base_bitcast;
        GlobalValue* gv = dyn_cast<GlobalValue>(base_bound);
        Instruction* begin_inst = NULL;
        if(!gv) {
            
          begin_inst = dyn_cast<Instruction>(bb_incoming->getTerminator());
        }
        else {
          begin_inst = dyn_cast<Instruction>(bb_incoming->getParent()->begin()->begin());

        }

          

        assert(begin_inst && " begin_inst null?");
        incoming_tmp_base_bitcast = new BitCastInst(base_bound, m_void_ptr_type, 
                                                    base_bound->getName() + ".base", begin_inst);

        phi_node->addIncoming(incoming_tmp_base_bitcast, bb_incoming);
        base_bound_map[map_index] = incoming_tmp_base_bitcast;
        return;
      }
    }

    /* check if it is a global, then add base and bound at the beginning of the function */

    BitCastInst* incoming_tmp_base_bitcast;
    Instruction* terminator_inst = NULL;

    terminator_inst = dyn_cast<Instruction>(bb_incoming->getTerminator());
    assert(terminator_inst && "terminator inst null?");
      
    incoming_tmp_base_bitcast = new BitCastInst(base_bound, m_void_ptr_type, 
                                                base_bound->getName() + ".base", 
                                                terminator_inst);
      
    phi_node->addIncoming(incoming_tmp_base_bitcast, bb_incoming);
    base_bound_map[map_index] = incoming_tmp_base_bitcast;

  }
  else{
    phi_node->addIncoming(base_bound, bb_incoming);
  }
}


void SoftBoundCETSPass::handleSelect(SelectInst* select_ins, int pass) {

  if(!isa<PointerType>(select_ins->getType())) 
    return;
    
  Value* condition = select_ins->getOperand(0);
    
  Value* operand_base[2];
  Value* operand_bound[2];    
  Value* operand_key[2];
  Value* operand_lock[2];

  for(unsigned m = 0; m < 2; m++) {
    Value* operand = select_ins->getOperand(m+1);
    
    if(spatial_safety){
      operand_base[m] = NULL;
      operand_bound[m] = NULL;
      if(checkBaseBoundMetadataPresent(operand)){      
        operand_base[m] = getAssociatedBase(operand);
        operand_bound[m] = getAssociatedBound(operand);
      }
      
      if(isa<ConstantPointerNull>(operand) && !checkBaseBoundMetadataPresent(operand)) {            
        operand_base[m] = m_void_null_ptr;
        operand_bound[m] = m_void_null_ptr;
      }        
        
      Constant* given_constant = dyn_cast<Constant>(operand);
      
      if(given_constant) {
        getConstantExprBaseBound(given_constant, operand_base[m], operand_bound[m]);     
      }    
      assert(operand_base[m] != NULL && "operand doesn't have base with select?");
      assert(operand_bound[m] != NULL && "operand doesn't have bound with select?");
      
      /* Introduce a bit cast if the types don't match */
      
      if(operand_base[m]->getType() != m_void_ptr_type) {          
        operand_base[m] = new BitCastInst(operand_base[m], m_void_ptr_type,
                                          "select.base", select_ins);          
      }
      
      if(operand_bound[m]->getType() != m_void_ptr_type) {
        operand_bound[m] = new BitCastInst(operand_bound[m], m_void_ptr_type,
                                           "select_bound", select_ins);
      }
    } /* spatial safety ends */
    
    if(temporal_safety){
      operand_key[m] = NULL;
      operand_lock[m] = NULL;
      if(checkKeyLockMetadataPresent(operand)){
        operand_key[m] = getAssociatedKey(operand);
        Value* func_lock = getAssociatedFuncLock(select_ins);
        operand_lock[m] = getAssociatedLock(operand, func_lock);
      }

      if(isa<ConstantPointerNull>(operand) && !checkKeyLockMetadataPresent(operand)){
        operand_key[m] = m_constantint64ty_zero;
        operand_lock[m] = m_void_null_ptr;
      }

      Constant* given_constant = dyn_cast<Constant>(operand);
      if(given_constant){
        operand_key[m] = m_constantint64ty_one;
        operand_lock[m] = m_func_global_lock[select_ins->getParent()->getParent()->getName()];
      }

      assert(operand_key[m] != NULL && "operand doesn't have key with select?");
      assert(operand_lock[m] != NULL && "operand doesn't have lock with select?");
    }/* temporal safety ends */
  
  }/* for loop ends */
    

  if(spatial_safety){
      
    SelectInst* select_base = SelectInst::Create(condition, operand_base[0], 
                                                 operand_base[1], "select.base",
                                                 select_ins);
    
    SelectInst* select_bound = SelectInst::Create(condition, operand_bound[0], 
                                                  operand_bound[1], "select.bound",
                                                  select_ins);
  
    
    associateBaseBound(select_ins, select_base, select_bound);
  }

  if(temporal_safety){

    SelectInst* select_key = SelectInst::Create(condition, operand_key[0], 
                                                operand_key[1], "select.key",
                                                 select_ins);
    
    SelectInst* select_lock = SelectInst::Create(condition, operand_lock[0], 
                                                 operand_lock[1], "select.lock",
                                                 select_ins);

    associateKeyLock(select_ins, select_key, select_lock);
  }

}

bool SoftBoundCETSPass::checkBaseBoundMetadataPresent(Value* pointer_operand){

  if(m_pointer_base.count(pointer_operand) && m_pointer_bound.count(pointer_operand)){
      return true;
  }
  return false;
}

bool SoftBoundCETSPass::checkKeyLockMetadataPresent(Value* pointer_operand){

  if(m_pointer_key.count(pointer_operand) && m_pointer_lock.count(pointer_operand)){
      return true;
  }
  return false;
}


void SoftBoundCETSPass:: handleReturnInst(ReturnInst* ret){

  Value* pointer = ret->getReturnValue();
  if(pointer == NULL){
    return;
  }
  if(isa<PointerType>(pointer->getType())){
    introduceShadowStackStores(pointer, ret, 0);
  }
}


void SoftBoundCETSPass::handleGlobalSequentialTypeInitializer(Module& module, GlobalVariable* gv) {


  
  /* Sequential type can be an array type, a pointer type */
  const SequentialType* init_seq_type = dyn_cast<SequentialType>((gv->getInitializer())->getType());
  assert(init_seq_type && "[handleGlobalSequentialTypeInitializer] initializer sequential type null?");

  Instruction* init_function_terminator = getGlobalInitInstruction(module);
  if(gv->getInitializer()->isNullValue())
    return;
    
  if(isa<ArrayType>(init_seq_type)){      
    const ArrayType* init_array_type = dyn_cast<ArrayType>(init_seq_type);     
    if(isa<StructType>(init_array_type->getElementType())){
      /* it is a array of structures */

      /* Check whether the structure has a pointer, if it has a
       * pointer then, we need to store the base and bound of the
       * pointer into the metadata space. However, if the structure
       * does not have any pointer, we can make a quick exit in
       * processing this global
       */

      bool struct_has_pointers = false;
      StructType* init_struct_type = dyn_cast<StructType>(init_array_type->getElementType());
      CompositeType* struct_comp_type = dyn_cast<CompositeType>(init_struct_type);
      
      
      assert(struct_comp_type && "struct composite type null?");
      assert(init_struct_type && "Array of structures and struct type null?");        
      unsigned num_struct_elements = init_struct_type->getNumElements();        
      for(unsigned i = 0; i < num_struct_elements; i++) {
        Type* element_type = struct_comp_type->getTypeAtIndex(i);
        if(isa<PointerType>(element_type)){
          struct_has_pointers = true;
        }
      }
      if(!struct_has_pointers)
        return;

      /* Here implies, global variable is an array of structures
       * with a pointer. Thus for each pointer we need to store the
       * base and bound
       */

      size_t num_array_elements = init_array_type->getNumElements();

      ConstantArray* const_array = dyn_cast<ConstantArray>(gv->getInitializer());
      if(!const_array)
        return;

      for( unsigned i = 0; i < num_array_elements ; i++) {
        Constant* struct_constant = const_array->getOperand(i);
        assert(struct_constant && "Initializer structure type but not a constant?");          
        /* constant has zero initializer */
        if(struct_constant->isNullValue())
          continue;
          
        for( unsigned j = 0 ; j < num_struct_elements; j++) {
            
          const Type* element_type = init_struct_type->getTypeAtIndex(j);
            
          if(isa<PointerType>(element_type)){
              
            Value* initializer_opd = struct_constant->getOperand(j);
            Value* operand_base = NULL;
            Value* operand_bound = NULL;
            Constant* given_constant = dyn_cast<Constant>(initializer_opd);
            assert(given_constant && "[handleGlobalStructTypeInitializer] not a constant?");
              
            getConstantExprBaseBound(given_constant, operand_base, operand_bound);            
            /* creating the address of ptr */
            Constant* index0 = ConstantInt::get(Type::getInt32Ty(module.getContext()), 0);
            Constant* index1 = ConstantInt::get(Type::getInt32Ty(module.getContext()), i);
            Constant* index2 = ConstantInt::get(Type::getInt32Ty(module.getContext()), j);
              
            std::vector<Constant *> indices_addr_ptr;            
                            
            indices_addr_ptr.push_back(index0);
            indices_addr_ptr.push_back(index1);
            indices_addr_ptr.push_back(index2);

            Constant* Indices[3] = {index0, index1, index2};
              
            Constant* addr_of_ptr = ConstantExpr::getGetElementPtr(gv, Indices);              
            
            Type* initializer_type = initializer_opd->getType();
            Value* initializer_size = getSizeOfType(initializer_type);
            
            Value* operand_key = NULL;
            Value* operand_lock = NULL;
            if(temporal_safety){
              operand_key = m_constantint_one;
              operand_lock = introduceGlobalLockFunction(init_function_terminator);
            }

            addStoreBaseBoundFunc(addr_of_ptr, operand_base, operand_bound, 
                                  operand_key, operand_lock, initializer_opd, 
                                  initializer_size, init_function_terminator);
          }                       
        } /* iterating over struct element ends */
      }/* Iterating over array element ends  */        
    }/* Array of Structures Ends */
    if(isa<PointerType>(init_array_type->getElementType())){
      /* it is a array of pointers */
    }
  }/* Array type case ends */

  if(isa<PointerType>(init_seq_type)){
    /* individual pointer stores */
    Value* initializer_base = NULL;
    Value* initializer_bound = NULL;
    Value* initializer = gv->getInitializer();
    Constant* given_constant = dyn_cast<Constant>(initializer);
    getConstantExprBaseBound(given_constant, initializer_base, initializer_bound);
    Type* initializer_type = initializer->getType();
    Value* initializer_size = getSizeOfType(initializer_type);
    
    Value* operand_key = NULL;
    Value* operand_lock = NULL;
    if(temporal_safety){
      operand_key = m_constantint_one;
      operand_lock = introduceGlobalLockFunction(init_function_terminator);
    }
    
    addStoreBaseBoundFunc(gv, initializer_base, initializer_bound, operand_key, 
                          operand_lock, initializer, initializer_size, 
                          init_function_terminator);        
  }

}


  /* handleGlobalStructTypeInitializer: handles the global
   * initialization for global variables which are of struct type and
   * have a pointer as one of their fields and is global
   * initialized 
   */

void SoftBoundCETSPass::handleGlobalStructTypeInitializer(Module& module, 
                                                          StructType* init_struct_type, 
                                                          Constant* initializer, 
                                                          GlobalVariable* gv, 
                                                          std::vector<Constant*> indices_addr_ptr, 
                                                          int length) {
  
  /* TODO:URGENT: Do I handle nested structures? */
  
  /* has zero initializer */
  if(initializer->isNullValue())
    return;
    
  Instruction* first = getGlobalInitInstruction(module);
  unsigned num_elements = init_struct_type->getNumElements();
  Constant* constant = dyn_cast<Constant>(initializer);
  assert(constant && "[handleGlobalStructTypeInitializer] global struct type with initializer but not a constant array?");

  for(unsigned i = 0; i < num_elements ; i++) {
    
    CompositeType* struct_comp_type = dyn_cast<CompositeType>(init_struct_type);
    assert(struct_comp_type && "not a struct type?");
    
    Type* element_type = struct_comp_type->getTypeAtIndex(i);      
    if(isa<PointerType>(element_type)){        
      Value* initializer_opd = constant->getOperand(i);
      Value* operand_base = NULL;
      Value* operand_bound = NULL;
      
      Value* operand_key = NULL;
      Value* operand_lock = NULL;
      
      Constant* addr_of_ptr = NULL;
      
      if(temporal_safety){
        operand_key = m_constantint_one;
        operand_lock = introduceGlobalLockFunction(first);
        
      }
      
      if(spatial_safety){
        Constant* given_constant = dyn_cast<Constant>(initializer_opd);
        assert(given_constant && "[handleGlobalStructTypeInitializer] not a constant?");
        
        getConstantExprBaseBound(given_constant, operand_base, operand_bound);      
        /* creating the address of ptr */
        //      Constant* index1 = ConstantInt::get(Type::getInt32Ty(module.getContext()), 0);
        Constant* index2 = ConstantInt::get(Type::getInt32Ty(module.getContext()), i);
        
        //      indices_addr_ptr.push_back(index1);
        indices_addr_ptr.push_back(index2);
        length++;
        addr_of_ptr = ConstantExpr::getGetElementPtr(gv, indices_addr_ptr);
      }   
      Type* initializer_type = initializer_opd->getType();
      Value* initializer_size = getSizeOfType(initializer_type);     
      addStoreBaseBoundFunc(addr_of_ptr, operand_base, operand_bound, operand_key, operand_lock, initializer_opd, initializer_size, first);
      
      if(spatial_safety){
        indices_addr_ptr.pop_back();
        length--;
      }

      continue;
    }     
    if(isa<StructType>(element_type)){
      StructType* child_element_type = dyn_cast<StructType>(element_type);
      Constant* struct_initializer = dyn_cast<Constant>(constant->getOperand(i));      
      Constant* index2 = ConstantInt::get(Type::getInt32Ty(module.getContext()), i);
      indices_addr_ptr.push_back(index2);
      length++;
      handleGlobalStructTypeInitializer(module, child_element_type, struct_initializer, gv, indices_addr_ptr, length); 
      indices_addr_ptr.pop_back();
      length--;
      continue;
    }
  }

}


/* A uniform function to handle all constant expressions */
void SoftBoundCETSPass::getConstantExprBaseBound(Constant* given_constant, 
                                             Value* & tmp_base,
                                             Value* & tmp_bound){

  
  ConstantExpr* cexpr = dyn_cast<ConstantExpr>(given_constant);
  tmp_base = NULL;
  tmp_bound = NULL;
    

  if(cexpr) {

    assert(cexpr && "ConstantExpr and Value* is null??");
    switch(cexpr->getOpcode()) {
        
    case Instruction::GetElementPtr:
      {
        Constant* internal_constant = dyn_cast<Constant>(cexpr->getOperand(0));
        getConstantExprBaseBound(internal_constant, tmp_base, tmp_bound);
        break;
      }
      
    case BitCastInst::BitCast:
      {
        Constant* internal_constant = dyn_cast<Constant>(cexpr->getOperand(0));
        getConstantExprBaseBound(internal_constant, tmp_base, tmp_bound);
        break;
      }
    case Instruction::IntToPtr:
      {
        tmp_base = m_void_null_ptr;
        tmp_bound = m_void_null_ptr;
        return;
        break;
      }
    default:
      {
        break;
      }
    } /* switch ends */
    
  } /* cexpr ends */
  else {
      
    const PointerType* func_ptr_type = dyn_cast<PointerType>(given_constant->getType());
      
    if(isa<FunctionType>(func_ptr_type->getElementType())) {
      tmp_base = m_void_null_ptr;
      tmp_bound = m_infinite_bound_ptr;
      return;
    }
    /* Create getElementPtrs to create the base and bound */

    std::vector<Constant*> indices_base;
    std::vector<Constant*> indices_bound;
      
    GlobalVariable* gv = dyn_cast<GlobalVariable>(given_constant);


    /* TODO: External globals get zero base and infinite_bound */

    if(gv && !gv->hasInitializer()) {
      tmp_base = m_void_null_ptr;
      tmp_bound = m_infinite_bound_ptr;
      return;
    }

    Constant* index_base0 = Constant::getNullValue(Type::getInt32Ty(given_constant->getType()->getContext()));
    Constant* index_bound0 = ConstantInt::get(Type::getInt32Ty(given_constant->getType()->getContext()), 1);

    indices_base.push_back(index_base0);
    indices_bound.push_back(index_bound0);


    // Constant* index1 = ConstantInt::get(Type::getInt32Ty(given_constant->getType()->getContext()), 0);
    // indices_base.push_back(index1);
    // indices_bound.push_back(index1);

    Constant* gep_base = ConstantExpr::getGetElementPtr(given_constant, indices_base);    
    Constant* gep_bound = ConstantExpr::getGetElementPtr(given_constant, indices_bound);
      
    tmp_base = gep_base;
    tmp_bound = gep_bound;      
  }


}



/* Function that returns the associated base Value* with the pointer
 * operand under consideration
 */

Value* SoftBoundCETSPass::getAssociatedBase(Value* pointer_operand) {
    
  if(isa<Constant>(pointer_operand)){
    Value* base = NULL;
    Value* bound = NULL;
    Constant* ptr_constant = dyn_cast<Constant>(pointer_operand);
    getConstantExprBaseBound(ptr_constant, base, bound);
    return base;
  }

  if(!m_pointer_base.count(pointer_operand)){
    pointer_operand->dump();
  }
  assert(m_pointer_base.count(pointer_operand) && "Pointer does not have a base entry in the map, probably because of dead code, try compiling with -simplifycfg option?");
    
  Value* pointer_base = m_pointer_base[pointer_operand];
  assert(pointer_base && "base present in the map but null?");

  if(pointer_base->getType() != m_void_ptr_type)
    assert(0 && "base in the map does not have the right type");

  return pointer_base;
}

/* Function that returns the associated bound Value* with the pointer
 * operand under consideration
 */

Value* SoftBoundCETSPass::getAssociatedBound(Value* pointer_operand) {

  if(isa<Constant>(pointer_operand)){
    Value* base = NULL;
    Value* bound = NULL;
    Constant* ptr_constant = dyn_cast<Constant>(pointer_operand);
    getConstantExprBaseBound(ptr_constant, base, bound);
    return bound;
  }

    
  assert(m_pointer_bound.count(pointer_operand) && "Pointer does not have a bound entry in the map?");
  Value* pointer_bound = m_pointer_bound[pointer_operand];
  assert(pointer_bound && "bound present in the map but null?");    

  if(pointer_bound->getType() != m_void_ptr_type)
    assert(0 && "bound in the map does not have the right type");

  return pointer_bound;
}


Value* SoftBoundCETSPass::getAssociatedKey(Value* pointer_operand) {
    
  if(!temporal_safety){
    return NULL;
  }

  if(isa<Constant>(pointer_operand)){
    return m_constantint_one;
  }

  if(!m_pointer_key.count(pointer_operand)){
    pointer_operand->dump();
  }
  assert(m_pointer_key.count(pointer_operand) && "Pointer does not have a base entry in the map, probably because of dead code, try compiling with -simplifycfg option?");
    
  Value* pointer_key = m_pointer_key[pointer_operand];
  assert(pointer_key && "key present in the map but null?");

  if(pointer_key->getType() != m_key_type)
    assert(0 && "key in the map does not have the right type");

  return pointer_key;
}

Value* SoftBoundCETSPass::getAssociatedLock(Value* pointer_operand, Value* func_lock) {
    
  if(!temporal_safety){
    return NULL;
  }

  if(isa<GlobalVariable>(pointer_operand)){
    return func_lock;
  }

  if(isa<Constant>(pointer_operand)){
    return func_lock;
  }

  if(!m_pointer_lock.count(pointer_operand)){
    pointer_operand->dump();
  }
  assert(m_pointer_lock.count(pointer_operand) && "Pointer does not have a base entry in the map, probably because of dead code, try compiling with -simplifycfg option?");
    
  Value* pointer_lock = m_pointer_lock[pointer_operand];
  assert(pointer_lock && "lock present in the map but null?");

  if(pointer_lock->getType() != m_void_ptr_type)
    assert(0 && "lock in the map does not have the right type");

  return pointer_lock;
}



std::string SoftBoundCETSPass::transformFunctionName(const std::string &str) { 

  // If the function name starts with this prefix, don't just
  // concatenate, but instead transform the string
  return "softboundcets_" + str; 
}

/* Member Function Definitions begin here */

void SoftBoundCETSPass::addMemcopyCheck(CallInst* call_inst) {

  if(!MEMCOPYCHECK) 
    return;
  
  /* FIXME do something here */
}

Value* SoftBoundCETSPass:: getSizeOfType(Type* input_type) {

  /* Create a Constant Pointer Null of the input type.
   * Then get a getElementPtr of it with next element access
   * cast it to unsigned int 
   */
  const PointerType* ptr_type = dyn_cast<PointerType>(input_type);

  if(isa<FunctionType>(ptr_type->getElementType())) {
    if(m_is_64_bit) {
      return ConstantInt::get(Type::getInt64Ty(ptr_type->getContext()), 0);
    }
    else{
      return ConstantInt::get(Type::getInt32Ty(ptr_type->getContext()), 0);
    }
  }


  const SequentialType* seq_type = dyn_cast<SequentialType>(input_type);
  Constant* int64_size = NULL;
  assert(seq_type && "pointer dereference and it is not a sequential type\n");
  
  StructType* struct_type = dyn_cast<StructType>(input_type);

  if(struct_type){
    if(struct_type->isOpaque()){
      if(m_is_64_bit) {
        return ConstantInt::get(Type::getInt64Ty(seq_type->getContext()), 0);        
      }
      else {
        return ConstantInt::get(Type::getInt32Ty(seq_type->getContext()), 0);
      }
    }
  }
  
  if(m_is_64_bit) {
    int64_size = ConstantExpr::getSizeOf(seq_type->getElementType());
    return int64_size;
  }
  else {

    /* doing what ConstantExpr::getSizeOf() does */
    Constant* gep_idx = ConstantInt::get(Type::getInt32Ty(seq_type->getContext()), 1);
    Constant* gep = ConstantExpr::getGetElementPtr(ConstantExpr::getNullValue(PointerType::getUnqual(seq_type->getElementType())), gep_idx);

    return ConstantExpr::getPtrToInt(gep, Type::getInt64Ty(seq_type->getContext()));
  }    

  assert(0 && "not handled type?");

  return NULL;
}




void SoftBoundCETSPass::addLoadStoreChecks(Instruction* load_store, std::map<Value*, int>& func_deref_check_elim_map) {

  if(!spatial_safety)
    return;

  SmallVector<Value*, 8> args;
  Value* pointer_operand = NULL;
    
  if(isa<LoadInst>(load_store)) {
    if(!LOADCHECKS)
      return;

    LoadInst* ldi = dyn_cast<LoadInst>(load_store);
    assert(ldi && "not a load instruction");
    pointer_operand = ldi->getPointerOperand();
  }
    
  if(isa<StoreInst>(load_store)){
    if(!STORECHECKS)
      return;
      
    StoreInst* sti = dyn_cast<StoreInst>(load_store);
    assert(sti && "not a store instruction");
    /* The pointer where the element is being stored is the second operand */
    pointer_operand = sti->getOperand(1);
  }
    
  assert(pointer_operand && "pointer operand null?");
    
  /* if it is a null pointer which is being loaded, then 
   * it must seg fault, no dereference check here 
   */
    
  if(isa<ConstantPointerNull>(pointer_operand))
    return;
  /* Find all uses of pointer operand, then check if it
   * dominates and if so, make a note in the map
   */

  GlobalVariable* gv = dyn_cast<GlobalVariable>(pointer_operand);    
  if(gv && GLOBALCONSTANTOPT) {
    return;
  }
    
  if(BOUNDSCHECKOPT) {
    /* Enable dominator based dereference check optimization only
     * when suggested 
     */
    if(func_deref_check_elim_map.count(load_store)) {
      return;
    }
                      
    /* iterate over the uses */            
    for(Value::use_iterator ui = pointer_operand->use_begin(), ue = pointer_operand->use_end(); ui != ue; ++ui) {
        
      Instruction* temp_inst = dyn_cast<Instruction>(*ui);       
      if(!temp_inst)
        continue;
        
      if(temp_inst == load_store)
        continue;

      if(!isa<LoadInst>(temp_inst) && !isa<StoreInst>(temp_inst))
        continue;
        
      if(isa<StoreInst>(temp_inst)){
        if(temp_inst->getOperand(1) != pointer_operand){
          /* when a pointer is a being stored at at a particular
           * address, don't elide the check
           */
          continue;
        }
      }


      if(m_dominator_tree->dominates(load_store, temp_inst)) {
        if(!func_deref_check_elim_map.count(temp_inst)) {
          func_deref_check_elim_map[temp_inst] = true;
          continue;
        }                  
      }


    } /* Iterating over uses ends */
  } /* BOUNDSCHECKOPT ends */

    
  Value* tmp_base = NULL;
  Value* tmp_bound = NULL;
    
  Constant* given_constant = dyn_cast<Constant>(pointer_operand);    
  if(given_constant ) {
    if(GLOBALCONSTANTOPT)
      return;      

    getConstantExprBaseBound(given_constant, tmp_base, tmp_bound);
  }
  else {
    tmp_base = getAssociatedBase(pointer_operand);
    tmp_bound = getAssociatedBound(pointer_operand);
  }

  Value* bitcast_base = castToVoidPtr(tmp_base, load_store);
  args.push_back(bitcast_base);
  
  Value* bitcast_bound = castToVoidPtr(tmp_bound, load_store);    
  args.push_back(bitcast_bound);
   
  Value* cast_pointer_operand_value = castToVoidPtr(pointer_operand, load_store);    
  args.push_back(cast_pointer_operand_value);
    
  /* pushing the size of the type */
  Type* pointer_operand_type = pointer_operand->getType();
  Value* size_of_type = getSizeOfType(pointer_operand_type);
  args.push_back(size_of_type);

  if(isa<LoadInst>(load_store)){
    CallInst::Create(m_spatial_load_dereference_check, args, "", load_store);
  }
  else{    
    CallInst::Create(m_spatial_store_dereference_check, args, "", load_store);
  }
  return;
}

bool SoftBoundCETSPass::optimizeGlobalAndStackVariableChecks(Instruction* load_store) {
    
  Value* pointer_operand = NULL;

  if(isa<LoadInst>(load_store)){
    pointer_operand = load_store->getOperand(0);
  }
  else{
    pointer_operand = load_store->getOperand(1);
  }

  while(true) {
      
    if(isa<AllocaInst>(pointer_operand)){
        
      if(STACKTEMPORALCHECKOPT){
        return true;
      }
      else{
        return false;
      }
    }

    if(isa<GlobalVariable>(pointer_operand)){
        
      if(GLOBALTEMPORALCHECKOPT){
        return true;
      }
      else{
        return false;
      }
    }
      
    if(isa<BitCastInst>(pointer_operand)){
      BitCastInst* bitcast_inst = dyn_cast<BitCastInst>(pointer_operand);
      pointer_operand = bitcast_inst->getOperand(0);        
      continue;
    }

    if(isa<GetElementPtrInst>(pointer_operand)){
      GetElementPtrInst* gep_inst = dyn_cast<GetElementPtrInst>(pointer_operand);
      pointer_operand = gep_inst->getOperand(0); 
      continue;
    }
    else{
      return false;
    }
  }
}

bool SoftBoundCETSPass::bbTemporalCheckElimination(Instruction* load_store, std::map<Value*, int>& bb_temporal_check_elim_map){
    
  if(!BBDOMTEMPORALCHECKOPT)
    return false;

  if(bb_temporal_check_elim_map.count(load_store))
    return true;

  /* Check if the operand is a getelementptr, then get the first
     operand and check for all other load/store instructions in the
     current basic block and check if they are pointer operands are
     getelementptrs. If so, check if it is same the pointer being
     checked now 
  */
    
  Value* pointer_operand = getPointerLoadStore(load_store);

  Value* gep_source = NULL;
  if(isa<GetElementPtrInst>(pointer_operand)){

    GetElementPtrInst* ptr_gep_inst = dyn_cast<GetElementPtrInst>(pointer_operand);
    assert(ptr_gep_inst && "[bbTemporalCheckElimination] gep_inst null?");
      
    gep_source = ptr_gep_inst->getOperand(0);
  }
  else {
    gep_source = pointer_operand;
  }
    
  /* Iterate over all other instructions in this basic block and look for gep_instructions with the same source */
  BasicBlock* bb_curr = load_store->getParent();
  assert(bb_curr && "bb null?");

  Instruction* next_inst = getNextInstruction(load_store);
  BasicBlock* next_inst_bb = next_inst->getParent();
  while((next_inst_bb == bb_curr) && (next_inst != bb_curr->getTerminator())) {

    if(isa<CallInst>(next_inst) && OPAQUECALLS)
      break;
      
    if(checkLoadStoreSourceIsGEP(next_inst, gep_source)){
      bb_temporal_check_elim_map[next_inst] = 1;
    }

    next_inst = getNextInstruction(next_inst);
    next_inst_bb = next_inst->getParent();
  }
  return false;

}

Value* SoftBoundCETSPass::getPointerLoadStore(Instruction* load_store) {

  Value* pointer_operand  = NULL;

  if(isa<LoadInst>(load_store)){
    pointer_operand = load_store->getOperand(0);
  }

  if(isa<StoreInst>(load_store)){
    pointer_operand = load_store->getOperand(1);
  }
  assert((pointer_operand != NULL) && "pointer_operand null");

  return pointer_operand;
}


bool SoftBoundCETSPass::checkLoadStoreSourceIsGEP(Instruction* load_store, Value* gep_source){

  Value* pointer_operand = NULL;

  if(!isa<LoadInst>(load_store) && !isa<StoreInst>(load_store))
    return false;

  if(isa<LoadInst>(load_store)){
    pointer_operand = load_store->getOperand(0);
  }

  if(isa<StoreInst>(load_store)){
    pointer_operand = load_store->getOperand(1);
  }

  assert(pointer_operand && "pointer_operand null?");

  if(!isa<GetElementPtrInst>(pointer_operand))
    return false;

  GetElementPtrInst* gep_ptr = dyn_cast<GetElementPtrInst>(pointer_operand);
  assert(gep_ptr && "gep_ptr null?"); 

  Value* gep_ptr_operand = gep_ptr->getOperand(0);

  if(gep_ptr_operand == gep_source)    
    return true;

  return false;
}


bool SoftBoundCETSPass::funcTemporalCheckElimination(Instruction* load_store, std::map<Value*, int>& func_temporal_check_elim_map) {

  if(!FUNCDOMTEMPORALCHECKOPT)
    return false;

  if(func_temporal_check_elim_map.count(load_store))
    return true;

  Value* pointer_operand = getPointerLoadStore(load_store);

  Value* gep_source = NULL;
  if(isa<GetElementPtrInst>(pointer_operand)){

    GetElementPtrInst* ptr_gep_inst = dyn_cast<GetElementPtrInst>(pointer_operand);
    assert(ptr_gep_inst && "[bbTemporalCheckElimination] gep_inst null?");
    gep_source = ptr_gep_inst->getOperand(0);
  }
  else {
    gep_source = pointer_operand;
  }

  BasicBlock* bb_curr = load_store->getParent();
  assert(bb_curr && "bb null?");
          
  std::set<BasicBlock*> bb_visited;
  std::queue<BasicBlock*> bb_worklist;
      
  bb_worklist.push(bb_curr);
  BasicBlock* bb = NULL;
  while(bb_worklist.size() != 0){
      
    bb = bb_worklist.front();
    assert(bb && "Not a BasicBlock?");
      
    bb_worklist.pop();
    if(bb_visited.count(bb)){
      continue;
    }
    bb_visited.insert(bb);

    bool break_flag = false;

    /* Iterating over the successors and adding the successors to
     * the work list
     */

    /* if this is the current basic block under question */
    if(bb == bb_curr) {
      /* bbTemporalCheckElimination should handle this */
      Instruction* next_inst = getNextInstruction(load_store);
      BasicBlock* next_inst_bb = next_inst->getParent();
      while((next_inst_bb == bb_curr) && (next_inst != bb_curr->getTerminator())) {

        if(isa<CallInst>(next_inst) && OPAQUECALLS){
          break_flag = true;
          break;
        }
          
        if(checkLoadStoreSourceIsGEP(next_inst, gep_source)){
          if(m_dominator_tree->dominates(load_store, next_inst)){              
            func_temporal_check_elim_map[next_inst] = 1;
          }
        }
          
        next_inst = getNextInstruction(next_inst);
        next_inst_bb = next_inst->getParent();
      }
    }
    else {
      for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i){
        Instruction* new_inst = dyn_cast<Instruction>(i);
        if(isa<CallInst>(new_inst) && OPAQUECALLS){
          break_flag = true;
          break;
        }

          
        if(checkLoadStoreSourceIsGEP(new_inst, gep_source)){

          if(m_dominator_tree->dominates(load_store, new_inst)){
            func_temporal_check_elim_map[new_inst] = 1;
          }
        }          
      } /* iterating over the instructions in the basic block ends */
    }

    for(succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; ++si) {
        
      if(break_flag)
        break;
        
      BasicBlock* next_bb = *si;
      assert(next_bb && "Not a basic block and I am adding to the base and bound worklist?");
      bb_worklist.push(next_bb);
    }      
  } /* worklist algorithm ends */
  return false;
}



bool SoftBoundCETSPass::optimizeTemporalChecks(Instruction* load_store, std::map<Value*, int>& bb_temporal_check_elim_map, std::map<Value*, int>& func_temporal_check_elim_map) {
    
  if(optimizeGlobalAndStackVariableChecks(load_store))
    return true;

  if(bbTemporalCheckElimination(load_store, bb_temporal_check_elim_map))
    return true;

  if(funcTemporalCheckElimination(load_store, func_temporal_check_elim_map))
    return true;

  return false;

}




void SoftBoundCETSPass::addTemporalChecks(Instruction* load_store, std::map<Value*,int>& bb_temporal_check_elim_map, std::map<Value*,int>& func_temporal_check_elim_map) {
  
  SmallVector<Value*, 8> args;
  Value* pointer_operand = NULL;

  if(!temporal_safety)
    return;
  
  
  if(optimizeTemporalChecks(load_store, bb_temporal_check_elim_map, func_temporal_check_elim_map))
    return;
  
  if(isa<LoadInst>(load_store)) {
    if(!TEMPORALLOADCHECKS)
      return;
      
    LoadInst* ldi = dyn_cast<LoadInst>(load_store);
    assert(ldi && "not a load instruction");
    pointer_operand = ldi->getPointerOperand();
  }
  
  if(isa<StoreInst>(load_store)){
    if(!TEMPORALSTORECHECKS)
      return;
    
    StoreInst* sti = dyn_cast<StoreInst>(load_store);
    assert(sti && "not a store instruction");
    /* The pointer where the element is being stored is the second operand */
    pointer_operand = sti->getOperand(1);
  }
  
  assert(pointer_operand && "pointer_operand null?");
  
  if(isa<ConstantPointerNull>(pointer_operand))
    return;
  
  /* Temporal check optimizations go here */
  
  /* don't insert checks for globals and constant expressions */
  GlobalVariable* gv = dyn_cast<GlobalVariable>(pointer_operand);    
  if(gv) {
    return;
  }
  Constant* given_constant = dyn_cast<Constant>(pointer_operand);
  if(given_constant)
    return;
  
    
    /* Find all uses of pointer operand, then check if it
     * dominates and if so, make a note in the map
     */
    
    if(TEMPORALBOUNDSCHECKOPT) {
      /* Enable dominator based dereference check optimization only
       * when suggested 
       */
      
      if(func_temporal_check_elim_map.count(load_store)) {
        return;
      }
      
      /* iterate over the uses */            
      for(Value::use_iterator ui = pointer_operand->use_begin(), ue = pointer_operand->use_end(); ui != ue; ++ui) {
        
        Instruction* temp_inst = cast<Instruction>(*ui);       
        if(!temp_inst)
          continue;
        
        if(temp_inst == load_store)
          continue;
        
        if(!isa<LoadInst>(temp_inst) && !isa<StoreInst>(temp_inst))
          continue;
        
        if(isa<StoreInst>(temp_inst)){
          if(temp_inst->getOperand(1) != pointer_operand){
            /* when a pointer is a being stored at at a particular
             * address, don't elide the check
             */
            continue;
          }
        }
        
        if(m_dominator_tree->dominates(load_store, temp_inst)) {
          if(!func_temporal_check_elim_map.count(temp_inst)) {
            func_temporal_check_elim_map[temp_inst] = true;
            continue;
          }                  
        }
      } /* Iterating over uses ends */
    } /* TEMPORALBOUNDSCHECKOPT ends */


    Value* tmp_key = NULL;
    Value* tmp_lock = NULL;
    Value* tmp_base = NULL;
    Value* tmp_bound = NULL;

    tmp_key = getAssociatedKey(pointer_operand);
    Value* func_tmp_lock = getAssociatedFuncLock(load_store);
    tmp_lock = getAssociatedLock(pointer_operand, func_tmp_lock);

    if(spatial_safety){
      tmp_base = getAssociatedBase(pointer_operand);
      tmp_bound = getAssociatedBound(pointer_operand);
    }
    
    assert(tmp_key && "[addTemporalChecks] pointer does not have key?");
    assert(tmp_lock && "[addTemporalChecks] pointer does not have lock?");
    
    Value* bitcast_lock = castToVoidPtr(tmp_lock, load_store);
    args.push_back(bitcast_lock);
    
    args.push_back(tmp_key);

    if(spatial_safety){
      args.push_back(tmp_base);
      args.push_back(tmp_bound);
    }

    if(isa<LoadInst>(load_store)){
      CallInst::Create(m_temporal_load_dereference_check, args, "", load_store);
    }
    else {
      CallInst::Create(m_temporal_store_dereference_check, args, "", load_store);
    }
    
    return;
}



void SoftBoundCETSPass::addDereferenceChecks(Function* func) {


  m_dominator_tree = &getAnalysis<DominatorTree>(*func);


  /* intra-procedural load dererference check elimination map */
  std::map<Value*, int> func_deref_check_elim_map;
  std::map<Value*, int> func_temporal_check_elim_map;

  /* WorkList Algorithm for adding dereference checks. Each basic
   * block is visited only once. We start by visiting the current
   * basic block, then pushing all the successors of the current
   * basic block on to the queue if it has not been visited
   */
    
  std::set<BasicBlock*> bb_visited;
  std::queue<BasicBlock*> bb_worklist;
  Function:: iterator bb_begin = func->begin();

  BasicBlock* bb = dyn_cast<BasicBlock>(bb_begin);
  assert(bb && "Not a basic block  and I am adding dereference checks?");
  bb_worklist.push(bb);

    
  while(bb_worklist.size() != 0) {
      
    bb = bb_worklist.front();
    assert(bb && "Not a BasicBlock?");
    bb_worklist.pop();

    if(bb_visited.count(bb)) {
      /* Block already visited */
      continue;
    }

    /* If here implies basic block not visited */
    /* Insert the block into the set of visited blocks */
    bb_visited.insert(bb);

    /* Iterating over the successors and adding the successors to
     * the worklist
     */
    for(succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; ++si) {
        
      BasicBlock* next_bb = *si;
      assert(next_bb && "Not a basic block and I am adding to the base and bound worklist?");
      bb_worklist.push(next_bb);
    }

    /* basic block load deref check optimization */
    std::map<Value*, int> bb_deref_check_map;
    std::map<Value*, int> bb_temporal_check_elim_map;
    /* structure check optimization */
    std::map<Value*, int> bb_struct_check_opt;
            
    for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i){
      Value* v1 = dyn_cast<Value>(i);
      Instruction* new_inst = dyn_cast<Instruction>(i);

      /* Do the dereference check stuff */
      if(!m_present_in_original.count(v1))
        continue;

      if(isa<LoadInst>(new_inst)){
        addLoadStoreChecks(new_inst, func_deref_check_elim_map);
        addTemporalChecks(new_inst, bb_temporal_check_elim_map, func_temporal_check_elim_map);
        continue;
      }

      if(isa<StoreInst>(new_inst)){
        addLoadStoreChecks(new_inst, func_deref_check_elim_map);
        addTemporalChecks(new_inst, bb_temporal_check_elim_map, func_temporal_check_elim_map);
        continue;
      }

      /* check call through function pointers */
      if(isa<CallInst>(new_inst)) {
          
        if(!CALLCHECKS) {
          continue;
        }          
	  

        SmallVector<Value*, 8> args;
        CallInst* call_inst = dyn_cast<CallInst>(new_inst);
        Value* tmp_base = NULL;
        Value* tmp_bound = NULL;

        assert(call_inst && "call instruction null?");
          
        Function* func = call_inst->getCalledFunction();
        if(func){
          /* add memcopy checks if it is a memcopy function */
          addMemcopyCheck(call_inst);
          continue;
        }

        if(!INDIRECTCALLCHECKS)
          continue;

        /* TODO:URGENT : indirect function call checking commented
         * out for the time being to test other aspect of the code,
         * problem was with spec benchmarks perl and h264. They were
         * primarily complaining that the use of a function did not
         * have base and bound in the map
         */


        /* here implies its an indirect call */
        Value* indirect_func_called = call_inst->getOperand(0);
            
        Constant* func_constant = dyn_cast<Constant>(indirect_func_called);
        if(func_constant) {
          getConstantExprBaseBound(func_constant, tmp_base, tmp_bound);           
        }
        else {
          tmp_base = getAssociatedBase(indirect_func_called);
          tmp_bound = getAssociatedBound(indirect_func_called);
        }
        /* Add BitCast Instruction for the base */
        Value* bitcast_base = castToVoidPtr(tmp_base, new_inst);
        args.push_back(bitcast_base);
            
        /* Add BitCast Instruction for the bound */
        Value* bitcast_bound = castToVoidPtr(tmp_bound, new_inst);
        args.push_back(bitcast_bound);
        Value* pointer_operand_value = castToVoidPtr(indirect_func_called, new_inst);
        args.push_back(pointer_operand_value);            
        CallInst::Create(m_call_dereference_func, args, "", new_inst);
        continue;
      } /* Call check ends */
    }
  }    
}



void SoftBoundCETSPass::renameFunctions(Module& module){
    
  bool change = false;

  do{
    change = false;
    for(Module::iterator ff_begin = module.begin(), ff_end = module.end();
        ff_begin != ff_end; ++ff_begin){
        
      Function* func_ptr = dyn_cast<Function>(ff_begin);

      if(m_func_transformed.count(func_ptr->getName()) || 
         isFuncDefSoftBound(func_ptr->getName())){
        continue;
      }
        
      m_func_transformed[func_ptr->getName()] = true;
      m_func_transformed[transformFunctionName(func_ptr->getName())] = true;
      bool is_external = func_ptr->isDeclaration();
      renameFunctionName(func_ptr, module, is_external);
      change = true;
      break;
    }
  }while(change);
}

  
/* Renames a function by changing the function name to softboundcets_*
 */
  
void SoftBoundCETSPass:: renameFunctionName(Function* func, Module& module, bool external) {
    
  Type* ret_type = func->getReturnType();
  const FunctionType* fty = func->getFunctionType();
  std::vector<Type*> params;

  if(func->getName() == "softboundcets_pseudo_main")
    return;

  SmallVector<AttributeWithIndex, 8> param_attrs_vec;

#if 0

  const AttrListPtr& pal = func->getAttributes();
  if(Attributes attrs = pal.getRetAttributes())
    param_attrs_vec.push_back(AttributeWithIndex::get(0, attrs));
#endif

  int arg_index = 1;

  for(Function::arg_iterator i = func->arg_begin(), e = func->arg_end();
      i != e; ++i, arg_index++) {

    params.push_back(i->getType());
#if 0
    if(Attributes attrs = pal.getParamAttributes(arg_index))
      param_attrs_vec.push_back(AttributeWithIndex::get(params.size(), attrs));
#endif
  }

  FunctionType* nfty = FunctionType::get(ret_type, params, fty->isVarArg());
  Function* new_func = Function::Create(nfty, func->getLinkage(), transformFunctionName(func->getName()));
  new_func->copyAttributesFrom(func);
  new_func->setAttributes(AttrListPtr::get(param_attrs_vec.begin(), param_attrs_vec.end()));    
  func->getParent()->getFunctionList().insert(func, new_func);
    
  if(!external) {
    SmallVector<Value*, 16> call_args;      
    new_func->getBasicBlockList().splice(new_func->begin(), func->getBasicBlockList());      
    Function::arg_iterator arg_i2 = new_func->arg_begin();      
    for(Function::arg_iterator arg_i = func->arg_begin(), arg_e = func->arg_end(); 
        arg_i != arg_e; ++arg_i) {
        
      arg_i->replaceAllUsesWith(arg_i2);
      arg_i2->takeName(arg_i);        
      ++arg_i2;
      arg_index++;
    }
  }
  func->replaceAllUsesWith(new_func);                            
  func->eraseFromParent();
}


void SoftBoundCETSPass::handleAlloca (AllocaInst* alloca_inst,
                                            Value* alloca_key,
                                            Value* alloca_lock,
                                            Value* func_xmm_key_lock,
                                            BasicBlock* bb, 
                                            BasicBlock::iterator& i) {

  Value *alloca_inst_value = alloca_inst;

  if(spatial_safety){
    /* Get the base type of the alloca object For alloca instructions,
     * instructions need to inserted after the alloca instruction LLVM
     * provides interface for inserting before.  So use the iterators
     * and handle the case
     */
    
    BasicBlock::iterator nextInst = i;
    nextInst++;
    Instruction* next = dyn_cast<Instruction>(nextInst);
    assert(next && "Cannot increment the instruction iterator?");
    
    unsigned num_operands = alloca_inst->getNumOperands();
    
    /* For any alloca instruction, base is bitcast of alloca, bound is bitcast of alloca_ptr + 1
     */
    PointerType* ptr_type = PointerType::get(alloca_inst->getAllocatedType(), 0);
    Type* ty1 = ptr_type;
    //    Value* alloca_inst_temp_value = alloca_inst;
    BitCastInst* ptr = new BitCastInst(alloca_inst, ty1, alloca_inst->getName(), next);
    
    Value* ptr_base = castToVoidPtr(alloca_inst_value, next);
    
    Value* intBound;
    
    if(num_operands == 0) {
      if(m_is_64_bit) {      
        intBound = ConstantInt::get(Type::getInt64Ty(alloca_inst->getType()->getContext()), 1, false);
      }
      else{
        intBound = ConstantInt::get(Type::getInt32Ty(alloca_inst->getType()->getContext()), 1, false);
      }
    }
    else {
      intBound = alloca_inst->getOperand(0);
    }
    GetElementPtrInst* gep = GetElementPtrInst::Create(ptr,
                                                       intBound,
                                                       "mtmp",
                                                       next);
    Value *bound_ptr = gep;
    
    Value* ptr_bound = castToVoidPtr(bound_ptr, next);
    
    associateBaseBound(alloca_inst_value, ptr_base, ptr_bound);
  }
  
  if(temporal_safety){    
    associateKeyLock(alloca_inst_value, alloca_key, alloca_lock);
  }
}
   

void SoftBoundCETSPass::handleStore(StoreInst* store_inst) {

  Value* operand = store_inst->getOperand(0);
  Value* pointer_dest = store_inst->getOperand(1);
  Instruction* insert_at = getNextInstruction(store_inst);
    
  /* If a pointer is being stored, then the base and bound
   * corresponding to the pointer must be stored in the shadow space
   */
  if(!isa<PointerType>(operand->getType()))
    return;
      

  if(isa<ConstantPointerNull>(operand)) {
    /* it is a constant pointer null being stored
     * store null to the shadow space
     */
#if 0    
    StructType* ST = dyn_cast<StructType>(operand->getType());

    if(ST){
      if(ST->isOpaque()){
        DEBUG(errs()<<"Opaque type found\n");        
      }

    }
      Value* size_of_type = getSizeOfType(operand->getType());
#endif

      Value* size_of_type = NULL;

      addStoreBaseBoundFunc(pointer_dest, m_void_null_ptr, m_void_null_ptr, m_constantint64ty_zero, m_void_null_ptr, m_void_null_ptr, size_of_type, insert_at);

    return;      
  }

      
  /* if it is a global expression being stored, then add add
   * suitable base and bound
   */
    
  Value* tmp_base = NULL;
  Value* tmp_bound = NULL;
  Value* tmp_key = NULL;
  Value* tmp_lock = NULL;

  //  Value* xmm_base_bound = NULL;
  //  Value* xmm_key_lock = NULL;
    
  Constant* given_constant = dyn_cast<Constant>(operand);
  if(given_constant) {      
    if(spatial_safety){
      getConstantExprBaseBound(given_constant, tmp_base, tmp_bound);
      assert(tmp_base && "global doesn't have base");
      assert(tmp_bound && "global doesn't have bound");        
    }

    if(temporal_safety){
      tmp_key = m_constantint_one;
      Value* func_lock = m_func_global_lock[store_inst->getParent()->getParent()->getName()];
      tmp_lock = func_lock;
    } 
  }
  else {      
    /* storing an external function pointer */
    if(spatial_safety){
      if(!checkBaseBoundMetadataPresent(operand)) {
        return;
      }
    }

    if(temporal_safety){
      if(!checkKeyLockMetadataPresent(operand)){
        return;
      }
    }

    if(spatial_safety){
      tmp_base = getAssociatedBase(operand);
      tmp_bound = getAssociatedBound(operand);              
    }

    if(temporal_safety){
      tmp_key = getAssociatedKey(operand);
      Value* func_lock = getAssociatedFuncLock(store_inst);
      tmp_lock = getAssociatedLock(operand, func_lock);
    }
  }    
  
  /* Store the metadata into the metadata space
   */
  

  //  Type* stored_pointer_type = operand->getType();
  Value* size_of_type = NULL;
  //    Value* size_of_type  = getSizeOfType(stored_pointer_type);
  addStoreBaseBoundFunc(pointer_dest, tmp_base, tmp_bound, tmp_key, tmp_lock, operand,  size_of_type, insert_at);    
  
}

// Currently just a placeholder for functions introduced by us
bool SoftBoundCETSPass::checkIfFunctionOfInterest(Function* func) {

  if(isFuncDefSoftBound(func->getName()))
    return false;

  if(func->isDeclaration())
    return false;


  /* TODO: URGENT: Need to do base and bound propagation in variable
   * argument functions
   */
#if 0
  if(func.isVarArg())
    return false;
#endif

  return true;
}


Instruction* SoftBoundCETSPass:: getGlobalInitInstruction(Module& module){
  Function* global_init_function = module.getFunction("__softboundcets_global_init");    
  assert(global_init_function && "no __softboundcets_global_init function??");    
  Instruction *global_init_terminator = NULL;
  bool return_inst_flag = false;
  for(Function::iterator fi = global_init_function->begin(), fe = global_init_function->end(); fi != fe; ++fi) {
      
    BasicBlock* bb = dyn_cast<BasicBlock>(fi);
    assert(bb && "basic block null");
    Instruction* bb_term = dyn_cast<Instruction>(bb->getTerminator());
    assert(bb_term && "terminator null?");
      
    if(isa<ReturnInst>(bb_term)) {
      assert((return_inst_flag == false) && "has multiple returns?");
      return_inst_flag = true;
      global_init_terminator = dyn_cast<ReturnInst>(bb_term);
      assert(global_init_terminator && "return inst null?");
    }
  }
  assert(global_init_terminator && "global init does not have return, strange");
  return global_init_terminator;
}



void SoftBoundCETSPass::handleGEP(GetElementPtrInst* gep_inst) {
  Value* getelementptr_operand = gep_inst->getPointerOperand();
  propagateMetadata(getelementptr_operand, gep_inst, SBCETS_GEP);
}

void SoftBoundCETSPass::handleMemcpy(CallInst* call_inst){
    
  Function* func = call_inst->getCalledFunction();
  if(!func)
    return;

  assert(func && "function is null?");

  CallSite cs(call_inst);
  Value* arg1 = cs.getArgument(0);
  Value* arg2 = cs.getArgument(1);
  Value* arg3 = cs.getArgument(2);

  SmallVector<Value*, 8> args;
  args.push_back(arg1);
  args.push_back(arg2);
  args.push_back(arg3);

  if(arg3->getType() == Type::getInt64Ty(arg3->getContext())){
    CallInst::Create(m_copy_metadata, args, "", call_inst);
  }
  else{
    //    CallInst::Create(m_copy_metadata, args, "", call_inst);
  }
  args.clear();

#if 0

  Value* arg1_base = castToVoidPtr(getAssociatedBase(arg1), call_inst);
  Value* arg1_bound = castToVoidPtr(getAssociatedBound(arg1), call_inst);
  Value* arg2_base = castToVoidPtr(getAssociatedBase(arg2), call_inst);
  Value* arg2_bound = castToVoidPtr(getAssociatedBound(arg2), call_inst);
  args.push_back(arg1);
  args.push_back(arg1_base);
  args.push_back(arg1_bound);
  args.push_back(arg2);
  args.push_back(arg2_base);
  args.push_back(arg2_bound);
  args.push_back(arg3);

  CallInst::Create(m_memcopy_check,args.begin(), args.end(), "", call_inst);

#endif
  return;
    
}

void SoftBoundCETSPass:: iterateCallSiteIntroduceShadowStackStores(CallInst* call_inst){
    
  int pointer_args_return = getNumPointerArgsAndReturn(call_inst);

  if(pointer_args_return == 0)
    return;
    
  int pointer_arg_no = 1;

  CallSite cs(call_inst);
  for(unsigned i = 0; i < cs.arg_size(); i++){
    Value* arg_value = cs.getArgument(i);
    if(isa<PointerType>(arg_value->getType())){
      introduceShadowStackStores(arg_value, call_inst, pointer_arg_no);
      pointer_arg_no++;
    }
  }    
}



void SoftBoundCETSPass::handleCall(CallInst* call_inst) {

  // Function* func = call_inst->getCalledFunction();
  Value* mcall = call_inst;

#if 0
  CallingConv::ID id = call_inst->getCallingConv();


  if(id == CallingConv::Fast){
    printf("fast calling convention not handled\n");
    exit(1);
  }
#endif 
    
  Function* func = call_inst->getCalledFunction();
  if(func && func->getName().find("llvm.memcpy") == 0){
    handleMemcpy(call_inst);
    return;
  }

  if(func && isFuncDefSoftBound(func->getName())){

    if(spatial_safety){
      associateBaseBound(call_inst, m_void_null_ptr, m_void_null_ptr);
    }
    if(temporal_safety){
      associateKeyLock(call_inst, m_constantint64ty_zero, m_void_null_ptr);
    }
    return;
  }

  Instruction* insert_at = getNextInstruction(call_inst);
  //  call_inst->setCallingConv(CallingConv::C);

  introduceShadowStackAllocation(call_inst);
  iterateCallSiteIntroduceShadowStackStores(call_inst);
    
  if(isa<PointerType>(mcall->getType())) {

      /* ShadowStack for the return value is 0 */
      introduceShadowStackLoads(call_inst, insert_at, 0);       
  }
  introduceShadowStackDeallocation(call_inst,insert_at);
}

void SoftBoundCETSPass::handleIntToPtr(IntToPtrInst* inttoptrinst) {
    
  Value* inst = inttoptrinst;
    
  if(spatial_safety){
    associateBaseBound(inst, m_void_null_ptr, m_void_null_ptr);
  }
  
  if(temporal_safety){
    associateKeyLock(inst, m_constantint64ty_zero, m_void_null_ptr);
  }
}


void SoftBoundCETSPass::gatherBaseBoundPass2(Function* func){

  /* WorkList Algorithm for propagating base and bound. Each basic
   * block is visited only once
   */
  std::set<BasicBlock*> bb_visited;
  std::queue<BasicBlock*> bb_worklist;
  Function::iterator bb_begin = func->begin();

  BasicBlock* bb = dyn_cast<BasicBlock>(bb_begin);
  assert(bb && "Not a basic block and gathering base bound in the next pass?");
  bb_worklist.push(bb);
    
  while( bb_worklist.size() != 0) {

    bb = bb_worklist.front();
    assert(bb && "Not a BasicBlock?");

    bb_worklist.pop();
    if( bb_visited.count(bb)) {
      /* Block already visited */

      continue;
    }
    /* If here implies basic block not visited */
      
    /* Insert the block into the set of visited blocks */
    bb_visited.insert(bb);

    /* Iterating over the successors and adding the successors to
     * the work list
     */
    for(succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; ++si) {

      BasicBlock* next_bb = *si;
      assert(next_bb && "Not a basic block and I am adding to the base and bound worklist?");
      bb_worklist.push(next_bb);
    }

    for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i) {
      Value* v1 = dyn_cast<Value>(i);
      Instruction* new_inst = dyn_cast<Instruction>(i);

      // If the instruction is not present in the original, no instrumentaion
      if(!m_present_in_original.count(v1))
        continue;

      switch(new_inst->getOpcode()) {

      case Instruction::GetElementPtr:
        {
          GetElementPtrInst* gep_inst = dyn_cast<GetElementPtrInst>(v1);         
          assert(gep_inst && "Not a GEP instruction?");
          handleGEP(gep_inst);
        }
        break;
          
      case Instruction::Store:
        {
          StoreInst* store_inst = dyn_cast<StoreInst>(v1);
          assert(store_inst && "Not a Store instruction?");
          handleStore(store_inst);
        }
        break;

      case Instruction::PHI:
        {
          PHINode* phi_node = dyn_cast<PHINode>(v1);
          assert(phi_node && "Not a PHINode?");
          handlePHIPass2(phi_node);
        }
        break;
 
      case BitCastInst::BitCast:
        {
          BitCastInst* bitcast_inst = dyn_cast<BitCastInst>(v1);
          assert(bitcast_inst && "Not a bitcast instruction?");
          handleBitCast(bitcast_inst);
        }
        break;

      case SelectInst::Select:
        {
        }
        break;
          
      default:
        break;
      }/* Switch Ends */
    }/* BasicBlock iterator Ends */
  }/* Function iterator Ends */
}

void SoftBoundCETSPass::introspectMetadata(Function* func, Value* ptr_value, Instruction* insert_at, int arg_no){
  
  if(func->getName() != "quantum_gate1")
    return;

  Value* ptr_base = getAssociatedBase(ptr_value);
  Value* ptr_bound = getAssociatedBound(ptr_value);

  Value* ptr_value_cast = castToVoidPtr(ptr_value, insert_at);
  Value* ptr_base_cast = castToVoidPtr(ptr_base, insert_at);
  Value* ptr_bound_cast = castToVoidPtr(ptr_bound, insert_at);

  Value* argno_value;

  argno_value = ConstantInt::get(Type::getInt32Ty(ptr_value->getType()->getContext()), arg_no, false);
  
  SmallVector<Value*, 8> args;
  
  args.push_back(ptr_value_cast);
  args.push_back(ptr_base_cast);
  args.push_back(ptr_bound_cast);
  args.push_back(argno_value);

  CallInst::Create(m_introspect_metadata, args, "", insert_at);

}


void SoftBoundCETSPass::freeFunctionKeyLock(Function* func, Value* & func_key, Value* & func_lock, Value* & func_xmm_key_lock) {


  if(func_key == NULL && func_lock == NULL){
    return;
  }

  if((func_key == NULL && func_lock != NULL) && (func_key != NULL && func_lock == NULL)){
    assert(0 && "inconsistent key lock");
  }

  Function::iterator  bb_begin = func->begin();
  Instruction* next_inst = NULL;

  for(Function::iterator b = func->begin(), be = func->end(); b != be ; ++b) {

    BasicBlock* bb = dyn_cast<BasicBlock>(b);
    assert(bb && "basic block does not exist?");
      
    for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i) {
        
      next_inst = dyn_cast<Instruction>(i);

      if(!isa<ReturnInst>(next_inst))
        continue;
   
      ReturnInst* ret = dyn_cast<ReturnInst>(next_inst);
      /* Insert a call to deallocate key and lock*/
      SmallVector<Value*, 8> args;
      Instruction* first_inst_func = dyn_cast<Instruction>(func->begin()->begin());
      assert(first_inst_func && "function doesn't have any instruction ??");
      args.push_back(func_key);
      CallInst::Create(m_temporal_stack_memory_deallocation, args, "", ret);
    }
  }
}



void SoftBoundCETSPass::gatherBaseBoundPass1 (Function * func) {

  Value* func_key = NULL;
  Value* func_lock = NULL;
  Value* func_xmm_key_lock = NULL;
  int arg_count= 0;
    
  //    std::cerr<<"transforming function with name:"<<func->getName()<< "\n";
  /* Scan over the pointer arguments and introduce base and bound */

  for(Function::arg_iterator ib = func->arg_begin(), ie = func->arg_end();
      ib != ie; ++ib) {

    if(!isa<PointerType>(ib->getType())) 
      continue;

    /* it is a pointer, so increment the arg count */
    arg_count++;

    Argument* ptr_argument = dyn_cast<Argument>(ib);
    Value* ptr_argument_value = ptr_argument;
    Instruction* fst_inst = func->begin()->begin();
      
    /* Urgent: Need to think about what we need to do about byval attributes */
    if(ptr_argument->hasByValAttr()){
      
      if(spatial_safety){
        associateBaseBound(ptr_argument_value, m_void_null_ptr, m_infinite_bound_ptr);
      }
      if(temporal_safety){
        Value* func_temp_lock = getAssociatedFuncLock(func->begin()->begin());      
        associateKeyLock(ptr_argument_value, m_constantint64ty_one, func_temp_lock);
      }
    }
    else{
      introduceShadowStackLoads(ptr_argument_value, fst_inst, arg_count);
      introspectMetadata(func, ptr_argument_value, fst_inst, arg_count);
    }
  }

  getFunctionKeyLock(func, func_key, func_lock, func_xmm_key_lock);


  /* WorkList Algorithm for propagating the base and bound. Each
   * basic block is visited only once. We start by visiting the
   * current basic block, then push all the successors of the
   * current basic block on to the queue if it has not been visited
   */
  std::set<BasicBlock*> bb_visited;
  std::queue<BasicBlock*> bb_worklist;
  Function:: iterator bb_begin = func->begin();

  BasicBlock* bb = dyn_cast<BasicBlock>(bb_begin);
  assert( bb && "Not a basic block and I am gathering base and bound?");
  bb_worklist.push(bb);

  while(bb_worklist.size() != 0) {

    bb = bb_worklist.front();
    assert(bb && "Not a BasicBlock?");

    bb_worklist.pop();
    if( bb_visited.count(bb)) {
      /* Block already visited */
      continue;
    }
    /* If here implies basic block not visited */
      
    /* Insert the block into the set of visited blocks */
    bb_visited.insert(bb);

    /* Iterating over the successors and adding the successors to
     * the work list
     */
    for(succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; ++si) {

      BasicBlock* next_bb = *si;
      assert(next_bb && "Not a basic block and I am adding to the base and bound worklist?");
      bb_worklist.push(next_bb);
    }
      
    for(BasicBlock::iterator i = bb->begin(), ie = bb->end(); i != ie; ++i){
      Value* v1 = dyn_cast<Value>(i);
      Instruction* new_inst = dyn_cast<Instruction>(i);


      /* If the instruction is not present in the original, no
       * instrumentaion 
       */
      if(!m_present_in_original.count(v1)) {
        continue;
      }

      /* All instructions have been defined here as defining it in
       * switch causes compilation errors. Assertions have been in
       * the inserted in the specific cases
       */

      switch(new_inst->getOpcode()) {
        
      case Instruction::Alloca:
        {
          AllocaInst* alloca_inst = dyn_cast<AllocaInst>(v1);
          assert(alloca_inst && "Not an Alloca inst?");
          handleAlloca(alloca_inst, func_key, func_lock, func_xmm_key_lock, bb, i);
        }
        break;

      case Instruction::Load:
        {
          LoadInst* load_inst = dyn_cast<LoadInst>(v1);            
          assert(load_inst && "Not a Load inst?");
          handleLoad(load_inst);
        }
        break;

      case Instruction::GetElementPtr:
        {
          GetElementPtrInst* gep_inst = dyn_cast<GetElementPtrInst>(v1);
          assert(gep_inst && "Not a GEP inst?");
          handleGEP(gep_inst);
        }
        break;
	
      case BitCastInst::BitCast:
        {
          BitCastInst* bitcast_inst = dyn_cast<BitCastInst>(v1);
          assert(bitcast_inst && "Not a BitCast inst?");
          handleBitCast(bitcast_inst);
        }
        break;

      case Instruction::PHI:
        {
          PHINode* phi_node = dyn_cast<PHINode>(v1);
          assert(phi_node && "Not a phi node?");
          //printInstructionMap(v1);
          handlePHIPass1(phi_node);
        }
        /* PHINode ends */
        break;
        
      case Instruction::Call:
        {
          CallInst* call_inst = dyn_cast<CallInst>(v1);
          assert(call_inst && "Not a Call inst?");
          handleCall(call_inst);
        }
        break;

      case Instruction::Select:
        {
          SelectInst* select_insn = dyn_cast<SelectInst>(v1);
          assert(select_insn && "Not a select inst?");
          int pass = 1;
          handleSelect(select_insn, pass);
        }
        break;

      case Instruction::Store:
        {
          break;
        }

      case Instruction::IntToPtr:
        {
          IntToPtrInst* inttoptrinst = dyn_cast<IntToPtrInst>(v1);
          assert(inttoptrinst && "Not a IntToPtrInst?");
          handleIntToPtr(inttoptrinst);
          break;
        }
      case Instruction::Ret:
        {
          ReturnInst* ret = dyn_cast<ReturnInst>(v1);
          assert(ret && "not a return inst?");
          handleReturnInst(ret);
        }
        break;

        
      default:
        if(isa<PointerType>(v1->getType()))
          assert(!isa<PointerType>(v1->getType())&&
                 " Generating Pointer and not being handled");
      }
    }/* Basic Block iterator Ends */
  } /* Function iterator Ends */

  if(temporal_safety){
    freeFunctionKeyLock(func, func_key, func_lock, func_xmm_key_lock);
  }
   
}


/* handleLoad Takes a load_inst If the load is through a pointer
 * which is a global then inserts base and bound for that global
 * Also if the loaded value is a pointer then loads the base and
 * bound for for the pointer from the shadow space
 */

void SoftBoundCETSPass::handleLoad(LoadInst* load_inst) { 

  AllocaInst* base_alloca;
  AllocaInst* bound_alloca;
  AllocaInst* key_alloca;
  AllocaInst* lock_alloca;

  SmallVector<Value*, 8> args;

  if(!isa<PointerType>(load_inst->getType()))
    return;

  Value* load_inst_value = load_inst;
  Value* pointer_operand = load_inst->getPointerOperand();
  Instruction* load = load_inst;    

  Instruction* insert_at = getNextInstruction(load);

  /* If the load returns a pointer, then load the base and bound
   * from the shadow space
   */
  Value* pointer_operand_bitcast =  castToVoidPtr(pointer_operand, insert_at);      
  Instruction* first_inst_func = dyn_cast<Instruction>(load_inst->getParent()->getParent()->begin()->begin());
  assert(first_inst_func && "function doesn't have any instruction and there is load???");
  
  /* address of pointer being pushed */
  args.push_back(pointer_operand_bitcast);
    

  if(spatial_safety){
    
    base_alloca = new AllocaInst(m_void_ptr_type, "base.alloca", first_inst_func);
    bound_alloca = new AllocaInst(m_void_ptr_type, "bound.alloca", first_inst_func);
  
    /* base */
    args.push_back(base_alloca);
    /* bound */
    args.push_back(bound_alloca);
  }

  if(temporal_safety){
    
    key_alloca = new AllocaInst(Type::getInt64Ty(load_inst->getType()->getContext()), "key.alloca", first_inst_func);
    lock_alloca = new AllocaInst(m_void_ptr_type, "lock.alloca", first_inst_func);

    args.push_back(key_alloca);
    args.push_back(lock_alloca);
  }
  
  CallInst::Create(m_load_base_bound_func, args, "", insert_at);
      
  if(spatial_safety){
    Instruction* base_load = new LoadInst(base_alloca, "base.load", insert_at);
    Instruction* bound_load = new LoadInst(bound_alloca, "bound.load", insert_at);
    associateBaseBound(load_inst_value, base_load, bound_load);      
  }

  if(temporal_safety){
    Instruction* key_load = new LoadInst(key_alloca, "key.load", insert_at);
    Instruction* lock_load = new LoadInst(lock_alloca, "lock.load", insert_at);    
    associateKeyLock(load_inst_value, key_load, lock_load);
  }
}




/* Identify the initial globals present in the program before we add
 * extra base and bound for all globals
 */
void SoftBoundCETSPass::identifyInitialGlobals(Module& module) {

  for(Module::global_iterator it = module.global_begin(), ite = module.global_end();
      it != ite; ++it) {
      
    GlobalVariable* gv = dyn_cast<GlobalVariable>(it);
    if(gv) {
      m_initial_globals[gv] = true;
    }      
  }
}

void SoftBoundCETSPass::addBaseBoundGlobals(Module& M){
  /* iterate over the globals here */

  for(Module::global_iterator it = M.global_begin(), ite = M.global_end(); it != ite; ++it){
    
    GlobalVariable* gv = dyn_cast<GlobalVariable>(it);
    
    if(!gv){
      continue;
    }

    if(gv->getSection() == "llvm.metadata"){
      continue;
    }
    if(gv->getName() == "llvm.global_ctors"){
      continue;
    }
    
    if(!gv->hasInitializer())
      continue;
    
    /* gv->hasInitializer() is true */
    
    Constant* initializer = dyn_cast<Constant>(it->getInitializer());
    ConstantArray* constant_array = dyn_cast<ConstantArray>(initializer);
    
    if(initializer && isa<CompositeType>(initializer->getType())){

      if(isa<StructType>(initializer->getType())){
        std::vector<Constant*> indices_addr_ptr;
        Constant* index1 = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
        indices_addr_ptr.push_back(index1);
        StructType* struct_type = dyn_cast<StructType>(initializer->getType());
        handleGlobalStructTypeInitializer(M, struct_type, initializer, gv, indices_addr_ptr, 1);
        continue;
      }
      
      if(isa<SequentialType>(initializer->getType())){
        handleGlobalSequentialTypeInitializer(M, gv);
      }
    }
    
    if(initializer && !constant_array){
      
      if(isa<PointerType>(initializer->getType())){
        //        std::cerr<<"Pointer type initializer\n";
      }
    }
    
    if(!constant_array)
      continue;
    
    int num_ca_opds = constant_array->getNumOperands();
    
    for(int i = 0; i < num_ca_opds; i++){
      Value* initializer_opd = constant_array->getOperand(i);
      Instruction* first = getGlobalInitInstruction(M);
      Value* operand_base = NULL;
      Value* operand_bound = NULL;
      
      Constant* global_constant_initializer = dyn_cast<Constant>(initializer_opd);
      if(!isa<PointerType>(global_constant_initializer->getType())){
        break;
      }
      getConstantExprBaseBound(global_constant_initializer, operand_base, operand_bound);
      
      SmallVector<Value*, 8> args;
      Constant* index1 = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
      Constant* index2 = ConstantInt::get(Type::getInt32Ty(M.getContext()), i);

      std::vector<Constant*> indices_addr_ptr;
      indices_addr_ptr.push_back(index1);
      indices_addr_ptr.push_back(index2);

      Constant* addr_of_ptr = ConstantExpr::getGetElementPtr(gv, indices_addr_ptr);
      Type* initializer_type = initializer_opd->getType();
      Value* initializer_size = getSizeOfType(initializer_type);
      
      Value* operand_key = NULL;
      Value* operand_lock = NULL;

      if(temporal_safety){
        operand_key = m_constantint_one;
        operand_lock = introduceGlobalLockFunction(first);
      }
      
      addStoreBaseBoundFunc(addr_of_ptr, operand_base, operand_bound, operand_key, operand_lock, initializer_opd, initializer_size, first);
      
    }
  }

}
void SoftBoundCETSPass::identifyOriginalInst (Function * func) {

  for(Function::iterator bb_begin = func->begin(), bb_end = func->end();
      bb_begin != bb_end; ++bb_begin) {

    for(BasicBlock::iterator i_begin = bb_begin->begin(),
          i_end = bb_begin->end(); i_begin != i_end; ++i_begin){

      Value* insn = dyn_cast<Value>(i_begin);
      if(!m_present_in_original.count(insn)) {
        m_present_in_original[insn] = 1;
      }
      else {
        assert(0 && "present in original map already has the insn?");
      }

      if(isa<PointerType>(insn->getType())) {
        if(!m_is_pointer.count(insn)){
          m_is_pointer[insn] = 1;
        }
      }
    } /* BasicBlock ends */
  }/* Function ends */
}

bool SoftBoundCETSPass::runOnModule(Module& module) {

  if(module.getPointerSize() == llvm::Module::Pointer64) {
    m_is_64_bit = true;
  }
  else {
    m_is_64_bit = false;
  }
  
  initializeSoftBoundVariables(module);
  transformMain(module);

  identifyFuncToTrans(module);

  identifyInitialGlobals(module);
  addBaseBoundGlobals(module);
  
  for(Module::iterator ff_begin = module.begin(), ff_end = module.end(); 
      ff_begin != ff_end; ++ff_begin){
    Function* func_ptr = dyn_cast<Function>(ff_begin);
    assert(func_ptr && "Not a function??");
    
    /* No instrumentation for functions introduced by us for updating
     * and retrieving the shadow space
     */
      
    if(!checkIfFunctionOfInterest(func_ptr)) {
      continue;
    }   
    /* Iterating over the instructions in the function to identify IR
     * instructions in the original program In this pass, the pointers
     * in the original program are also identified
     */
      
    identifyOriginalInst(func_ptr);
      
    /* iterate over all basic block and then each insn within a basic
     * block We make two passes over the IR for base and bound
     * propagation and one pass for dereference checks
     */

    if(temporal_safety){
      Value* func_global_lock = introduceGlobalLockFunction(func_ptr->begin()->begin());
      m_func_global_lock[func_ptr->getName()] = func_global_lock;      
    }
      
    gatherBaseBoundPass1(func_ptr);
    gatherBaseBoundPass2(func_ptr);

    addDereferenceChecks(func_ptr);            
  }


  renameFunctions(module);
  DEBUG(errs()<<"Done with SoftBoundCETSPass\n");
  
  /* print the external functions not wrapped */

  for(Module::iterator ff_begin = module.begin(), ff_end = module.end();
      ff_begin != ff_end; ++ff_begin){
    Function* func_ptr = dyn_cast<Function>(ff_begin);
    assert(func_ptr && "not a function??");

    if(func_ptr->isDeclaration()){
      if(!isFuncDefSoftBound(func_ptr->getName()) && !(m_func_wrappers_available.count(func_ptr->getName()))){
        DEBUG(errs()<<"External function not wrapped:"<<func_ptr->getName()<<"\n");
      }

    }    
  }
    
  return true;
}

