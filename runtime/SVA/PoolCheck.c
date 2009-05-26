/*===- PoolCheck.cpp - Implementation of poolcheck runtime ----------------===*/
/*                                                                            */
/*                       The LLVM Compiler Infrastructure                     */
/*                                                                            */
/* This file was developed by the LLVM research group and is distributed      */
/* under the University of Illinois Open Source License. See LICENSE.TXT for  */
/* details.                                                                   */
/*                                                                            */
/*===----------------------------------------------------------------------===*/
/*                                                                            */
/* This file implements the poolcheck interface w/ metapools and opaque       */
/* pool ids.                                                                  */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

#include "PoolCheck.h"
#include "PoolSystem.h"
#include "adl_splay.h"
#ifdef LLVA_KERNEL
#include <stdarg.h>
#endif
#define DEBUG(x) 

/* Flag whether we are ready to perform pool operations */
int pchk_ready = 0;

/* Flag whether to do profiling */
/* profiling only works if this library is compiled to a .o file, not llvm */
static const int do_profile = 0;

/* Flag whether to support out of bounds pointer rewriting */
static const int use_oob = 0;

/* Flag whether to print error messages on bounds violations */
static const int do_fail = 0;

/* Statistic counters */
int stat_poolcheck=0;
int stat_poolcheckarray=0;
int stat_poolcheckarray_i=0;
int stat_boundscheck=0;
int stat_boundscheck_i=0;
int stat_regio=0;
int stat_poolcheckio=0;

/* Global splay for holding the interrupt context */
void * ICSplay = 0;

/* Global splay for holding the integer states */
MetaPoolTy IntegerStatePool;

/* Global splay for holding the declared stacks */
void * StackSplay = 0;

struct node {
  void* left;
  void* right;
  char* key;
  char* end;
  void* tag;
};

#define maskaddr(_a) ((void*) ((unsigned)_a & ~(4096 - 1)))

static int isInCache(MetaPoolTy*  MP, void* addr) {
  addr = maskaddr(addr);
  if (!addr) return 0;
  if (MP->cache0 == addr)
    return 1;
  if (MP->cache1 == addr)
    return 2;
  if (MP->cache2 == addr)
    return 3;
  if (MP->cache3 == addr)
    return 4;
  return 0;
}

static void mtfCache(MetaPoolTy* MP, int ent) {
  void* z = MP->cache0;
  switch (ent) {
  case 2:
    MP->cache0 = MP->cache1;
    MP->cache1 = z;
    break;
  case 3:
    MP->cache0 = MP->cache1;
    MP->cache1 = MP->cache2;
    MP->cache2 = z;
    break;
  case 4:
    MP->cache0 = MP->cache1;
    MP->cache1 = MP->cache2;
    MP->cache2 = MP->cache3;
    MP->cache3 = z;
    break;
  default:
    break;
  }
  return;
}

static int insertCache(MetaPoolTy* MP, void* addr) {
  addr = maskaddr(addr);
  if (!addr) return 0;
  if (!MP->cache0) {
    MP->cache0 = addr;
    return 1;
  }
  else if (!MP->cache1) {
    MP->cache1 = addr;
    return 2;
  }
  else if (!MP->cache2) {
    MP->cache2 = addr;
    return 3;
  }
  else {
    MP->cache3 = addr;
    return 4;
  }
}

/*
 * Function: pchk_init()
 *
 * Description:
 *  Initialization function to be called when the memory allocator run-time
 *  intializes itself.
 *
 * Preconditions:
 *  1) The OS kernel is able to handle callbacks from the Execution Engine.
 */
void
pchk_init(void) {
  // Index variable for loops
  int index;

  /* initialize runtime */
  adl_splay_libinit(poolcheckmalloc);

  /*
   * Initialize the Integer State Pool.
   */
  IntegerStatePool.Slabs = 0;
  IntegerStatePool.Objs = 0;
  IntegerStatePool.Functions = 0;
  IntegerStatePool.OOB = 0;
#ifdef SVA_IO
  IntegerStatePool.IOObjs = 0;
#endif
  for (index = 0; index < 4; ++index) {
    IntegerStatePool.cache[index] = (void *)0;
  }

  /*
   * Register all of the global variables in their respective meta pools.
   */
  asm __volatile__ ("call sc.register_globals");

  /*
   * Flag that we're pchk_ready to rumble!
   */
  pchk_ready = 1;
  return;
}

/* Register a slab */
void pchk_reg_slab(MetaPoolTy* MP, void* PoolID, void* addr, unsigned len) {
  if (!MP) { return; }
  PCLOCK();
  adl_splay_insert(&MP->Slabs, addr, len, PoolID);
  PCUNLOCK();
}

/* Remove a slab */
void pchk_drop_slab(MetaPoolTy* MP, void* PoolID, void* addr) {
  if (!MP) return;
  /* TODO: check that slab's tag is == PoolID */
  PCLOCK();
  adl_splay_delete(&MP->Slabs, addr);
  PCUNLOCK();
}

#ifdef SVA_MMU
extern void llva_reg_obj(void*, void*, unsigned, void *);
#endif

/* Register a non-pool allocated object */
void pchk_reg_obj(MetaPoolTy* MP, void* addr, unsigned len) {
  static int counter = 0;
  unsigned int index;
  if (!MP) { return; }
  PCLOCK();

#if 0
  {
  void * S = addr;
  unsigned len, tag = 0;
  if ((pchk_ready) && (adl_splay_retrieve(&MP->Objs, &S, &len, &tag)))
    poolcheckinfo2 ("regobj: Object exists", __builtin_return_address(0), tag);
  }
#endif

#if 0
  /* For debugging only */
  if (pchk_ready) {
    poolcheckinfo2 ("regobj: Created ", __builtin_return_address(0), addr);
  }
#endif
#ifdef SVA_MMU 
  llva_reg_obj(addr, MP, MP->TK, __builtin_return_address(0));
#endif
 
  adl_splay_insert(&MP->Objs, addr, len, __builtin_return_address(0));
#if 1
  /*
   * Look for an entry in the cache that matches.  If it does, just erase it.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] > addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
#endif
  PCUNLOCK();
}

/*
 * Function: pchk_reg_pages()
 *
 * Description:
 *  This is just like pchk_reg_obj(), except that the length is an order of the
 *  number of pages (e.g., 1 is 2^1 pages = 8192 bytes).
 */
void
pchk_reg_pages (MetaPoolTy* MP, void* addr, unsigned order) {
  pchk_reg_obj (MP, addr, 4096 * (1u << order));
}

/* Pointer to the beginning of the current stack */
static void * CurrentStackSplay = 0;

#ifdef SVA_KSTACKS
void
pchk_update_stack (void) {
  /*
   * Get the stack pointer.
   */
  unsigned int value;
  void * sp;
  __asm__ ("movl %%esp, %0\n" : "=r" (value));
  sp = (void *)(value);

  /*
   * Determine which stack it is in.
   */
  void * S = sp;
  unsigned stacktag = 0;
  if (adl_splay_retrieve(&(StackSplay), &S, 0, 0)) {
    CurrentStackSplay = &(((struct node *)(StackSplay))->tag);
  } else {
    CurrentStackSplay = 0;
  }
}
#endif

void
pchk_reg_stack (MetaPoolTy* MP, void* addr, unsigned len) {
  unsigned int index;
  if (!MP) { return; }
  PCLOCK();

  /*
   * Determine which stack this object is on and enter the MetaPool into the
   * splay tree of the stack object.
   */
  void * S = addr;
  unsigned stacktag = 0;
#ifdef SVA_KSTACKS
  if (CurrentStackSplay) {
    adl_splay_insert (CurrentStackSplay, MP, 1, 0);
  }
#endif

  /*
   * Insert the stack object into the MetaPool's splay tree.
   * Set the tag to the beginning of the stack that it is in; this will allow
   * us to delete all stack objects corresponding to that stack when the stack
   * is deleted.
   */
  adl_splay_insert(&MP->Objs, addr, len, stacktag);

#if 1
  /*
   * Look for an entry in the cache that matches.  If it does, just erase it.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] > addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
#endif
  PCUNLOCK();
}

#ifdef SVA_IO
void
pchk_reg_io (MetaPoolTy* MP, void* addr, unsigned len, unsigned phys) {
  unsigned int index;
  if (!pchk_ready || !MP) return;
  PCLOCK();

  ++stat_regio;
  /*
   * Add the original physical object to the splay tree.
   */
  adl_splay_insert(&MP->IOObjs, phys, len, 0);

  /*
   * Add the virtual object created by the I/O object allocation to the splay
   * tree.  Put the starting physical address into the tag in case we index
   * from the virtual object to the physical object.
   */
  adl_splay_insert(&MP->IOObjs, addr, len, phys);
  PCUNLOCK();
}

void
pchk_drop_io (MetaPoolTy* MP, void* addr) {
  if (!MP) return;
  PCLOCK();
  adl_splay_delete(&MP->IOObjs, addr);
  PCUNLOCK();
}
#endif

void pchk_reg_ic (int sysnum, int a, int b, int c, int d, int e, int f, void* addr) {
  PCLOCK();
  adl_splay_insert(&ICSplay, addr, (28*4), 0);
  PCUNLOCK();
}

void pchk_reg_ic_memtrap (void * p, void* addr) {
  PCLOCK();
  adl_splay_insert(&ICSplay, addr, (28*4), 0);
  PCUNLOCK();
}

/*
 * Function: pchk_reg_int()
 *
 * Descripton:
 *  This function registers an integer state.
 *
 * Inputs:
 *  addr - The pointer to the integer state.
 *
 * Preconditions:
 *  The virtual machine must ensure that the pointer points to valid integer
 *  state.
 */
#ifdef SVA_KSTACKS
void
pchk_reg_int (void* addr) {
  unsigned int index;
  if (!pchk_ready) return;
  PCLOCK();

  /*
   * First, find the stack upon which this state is saved.
   */
  void * Stack = addr;
  unsigned len;
  if (adl_splay_retrieve(&StackSplay, &Stack, &len, 0)) {
    adl_splay_insert(&(IntegerStatePool.Objs), addr, 72, Stack);
  } else {
    poolcheckfail ("pchk_reg_int: Did not find containing stack", (unsigned)addr, (void*)__builtin_return_address(0));
  }

#if 1
  /*
   * Look for an entry in the cache that matches.  If it does, just erase it.
   */
  for (index=0; index < 4; ++index) {
    if ((IntegerStatePool.start[index] <= addr) &&
       (IntegerStatePool.start[index]+IntegerStatePool.length[index] >= addr)) {
      IntegerStatePool.start[index] = 0;
      IntegerStatePool.length[index] = 0;
      IntegerStatePool.cache[index] = 0;
    }
  }
#endif
  PCUNLOCK();
}

/*
 * Function: pchk_drop_int()
 *
 * Description:
 *  Mark the specified integer state as invalid.
 */
void
pchk_drop_int (void * addr) {
  unsigned int index;

  PCLOCK();
  adl_splay_delete(&(IntegerStatePool.Objs), addr);

  /*
   * See if the object is within the cache.  If so, remove it from the cache.
   */
  for (index=0; index < 4; ++index) {
    if ((IntegerStatePool.start[index] <= addr) &&
       (IntegerStatePool.start[index]+IntegerStatePool.length[index] >= addr)) {
      IntegerStatePool.start[index] = 0;
      IntegerStatePool.length[index] = 0;
      IntegerStatePool.cache[index] = 0;
    }
  }
  PCUNLOCK();
}

/*
 * Function: pchk_check_int()
 *
 * Description:
 *  Verify whether the specified pointer points to a valid integer state.
 *
 * Inputs:
 *  addr - The integer state to check.
 *
 * Return value:
 *  0 - The pointer does not point to valid integer state.
 *  1 - The pointer does point to valid integer state.
 */
unsigned int
pchk_check_int (void* addr) {
  if (!pchk_ready) return 1;

  PCLOCK();

  void * S = addr;
  unsigned len, tag;
  unsigned int found = 0;

  if (adl_splay_retrieve(&IntegerStatePool.Objs, &S, &len, &tag))
    if (addr == S)
      found =  1;

  PCUNLOCK();

  return found;
}

/*
 * Function: pchk_declarestack()
 *
 * Description:
 *  Add a declared stack to the set of valid stacks.  The start and size of the
 *  stack must exactly match the start and size of one allocated heap or global
 *  object.  Stack objects are invalid; regions within fully registered objects
 *  are also invalid.
 *
 * Note:
 *  The tag field of the Stack splay tree is actually another splay tree that
 *  contains the set of MetaPools that contain objects registered on this stack.
 */
void
pchk_declarestack (void * MPv, unsigned char * addr, unsigned size) {
  MetaPoolTy * MP = (MetaPoolTy *)(MPv);

  /*
   * First, ensure that this stack has not been allocated within another
   * pre-existing stack.
   */
  if (adl_splay_find(&(StackSplay), addr)) {
    poolcheckfail ("pchk_declarestack: Stack already registered", (unsigned)addr, (void*)__builtin_return_address(0));
  }

  /*
   * Ensure that the stack object is the only object within the allocated
   * region.
   */
  void * S = addr;
  unsigned objlen, objtag;
  if (adl_splay_retrieve(&MP->Objs, &S, &objlen, &objtag)) {
    if (S != addr)
      poolcheckfail ("pchk_declarestack: Stack does not match allocated object start", (unsigned)addr, (void*)S);
    if (objlen != size)
      poolcheckfail ("pchk_declarestack: Stack does not match allocated object length", (unsigned)size, (void*)objlen);
  } else {
    poolcheckfail ("pchk_declarestack: Can't find object from which stack is allocated", (unsigned)addr, (void*)__builtin_return_address(0));
  }

  /*
   * Add the stack into the splay of stacks.
   */
  adl_splay_insert(&(StackSplay), addr, size, 0);

  return;
}

/*
 * Function: pchk_releasestack()
 *
 * Description:
 *  Mark the given stack as invalid.  This will invalidate all stack objects
 *  on the stack that are currently registered with the execution engine.
 *
 * Algorithm:
 *  o Ensure that we're not invaliding the stack currently within use.
 *  o Invalidate all stack objects registered on the stack.
 *  o Invalidate the stack object in the stack splay.
 */
void
pchk_releasestack (void * addr) {
  void * S = addr;
  unsigned int len;

  if (adl_splay_retrieve(&(StackSplay), &S, &len, 0)) {
    /*
     * Ensure that we're not trying to release the currently used stack.
     */
    unsigned char * stackp;
    __asm__ ("movl %%esp, %0\n" : "=r" (stackp));
    if ((S <= stackp) && (stackp < (S+len))) {
      poolcheckfail ("pchk_releasestack: Releasing current stack", (unsigned)addr, (void*)__builtin_return_address(0));
    }

    /*
     * Deregister all stack objects associated with this stack.
     */
    struct node ** MPSplay = (struct node **) &(((struct node *)(StackSplay))->tag);
    while (adl_splay_size (MPSplay)) {
      void * MP = (*MPSplay)->key;
      /* Delete all stack objects belonging to this stack. */
      if (MP) adl_splay_delete_tag (MP, S);

      /* Delete this node out of the set of MetaPools */
      adl_splay_delete (MPSplay, MP);
    }

    /*
     * Delete any saved integer state on the stack.
     */
    adl_splay_delete_tag (&(IntegerStatePool.Objs), S);
  } else {
    poolcheckfail ("pchk_releasestack: Invalid stack", (unsigned)addr, (void*)__builtin_return_address(0));
  }

  /*
   * Delete the stack itself.
   */
  adl_splay_delete(&(StackSplay), addr);
  return;
}

/*
 * Function: pchk_checkstack()
 *
 * Description:
 *  Ensure that the given pointer is within a declared stack.  If it is, return
 *  information about the stack.
 *
 * Inputs:
 *  addr   - The pointer to check
 *
 * Output:
 *  length - The size in bytes of the stack will be returned in the location
 *           pointed to by length.
 *
 * Return value:
 *  NULL - The given pointer does not point into a stack.
 *  Otherwise, a pointer to the beginning of the stack is returned.
 */
void *
pchk_checkstack (void * addr, unsigned int * length) {
  void * S = addr;
  unsigned int len;
  if (adl_splay_retrieve(&(StackSplay), &S, &len, 0)) {
    *length = len;
    return S;
  }

  return 0;
}
#endif

/*
 * Function:
 *  pchk_drop_obj()
 *
 * Description:
 *  Check that the specified object can be freed and invalidate its bounds in
 *  the specified MetaPool.
 */
void
pchk_drop_obj (MetaPoolTy* MP, void* addr) {
  unsigned int index;
  if (!MP) return;

  PCLOCK();

  /*
   * Ensure that the object is not a declared stack.
   */
#ifdef SVA_KSTACKS
  if (adl_splay_find (&StackSplay, addr)) {
    poolcheckfail ("pchk_drop_obj: Releasing declared stack",
                   (unsigned)addr,
                   (void*)__builtin_return_address(0));
    PCUNLOCK();
    return;
  }
#endif

  /*
   * Delete the object from the splay tree.
   */
  adl_splay_delete(&MP->Objs, addr);

  /*
   * See if the object is within the cache.  If so, remove it from the cache.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] > addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
  PCUNLOCK();
}

void pchk_drop_stack (MetaPoolTy* MP, void* addr) {
  unsigned int index;
  if (!MP) return;
  PCLOCK();
  adl_splay_delete(&MP->Objs, addr);

  /*
   * See if the object is within the cache.  If so, remove it from the cache.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] > addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
  PCUNLOCK();
}

void pchk_drop_ic (void* addr) {
  PCLOCK();
  adl_splay_delete(&ICSplay, addr);
  PCUNLOCK();
}

/*
 * Function: pchk_drop_ic_interrupt()
 *
 * Description:
 *  Identical to pchk_drop_ic but takes an additional argument to make the
 *  assembly dispatching code easier and faster.
 */
void pchk_drop_ic_interrupt (int intnum, void* addr) {
  PCLOCK();
  adl_splay_delete(&ICSplay, addr);
  PCUNLOCK();
}

/*
 * Function: pchk_drop_ic_memtrap()
 *
 * Description:
 *  Identical to pchk_drop_ic but takes an additional argument to make the
 *  assembly dispatching code easier and faster.
 */
void pchk_drop_ic_memtrap (void * p, void* addr) {
  PCLOCK();
  adl_splay_delete(&ICSplay, addr);
  PCUNLOCK();
}

/*
 * Function: pchk_reg_func()
 *
 * Description:
 *  Register a set of function pointers with a MetaPool.
 */
void
pchk_reg_func (MetaPoolTy * MP, unsigned int num, void ** functable) {
  unsigned int index;
  unsigned int tag=0;

  for (index=0; index < num; ++index) {
    adl_splay_insert(&MP->Functions, functable[index], 1, &tag);
  }
}

/* Register a pool */
/* The MPLoc is the location the pool wishes to store the metapool tag for */
/* the pool PoolID is in at. */
/* MP is the actual metapool. */
void pchk_reg_pool(MetaPoolTy* MP, void* PoolID, void* MPLoc) {
  if(!MP) return;
  if(*(void**)MPLoc && *(void**)MPLoc != MP) {
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inf bug a): ", (unsigned)*(void**)MPLoc, (void*)__builtin_return_address(0));
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inf bug b): ", (unsigned) MP, (void*)__builtin_return_address(0));
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inf bug c): ", (unsigned) PoolID, (void*)__builtin_return_address(0));
  }

  *(void**)MPLoc = (void*) MP;
}

/* A pool is deleted.  free it's resources (necessary for correctness of checks) */
void pchk_drop_pool(MetaPoolTy* MP, void* PoolID) {
  if(!MP) return;
  PCLOCK();
  adl_splay_delete_tag(&MP->Slabs, PoolID);
  PCUNLOCK();
}

/*
 * Function: poolcheckalign()
 *
 * Description:
 *  Detremine whether the specified pointer is within the specified MetaPool
 *  and whether it is at the specified offset from the beginning on an
 *  object.
 */
void
poolcheckalign (MetaPoolTy* MP, void* addr, unsigned offset, unsigned size) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif

  /*
   * Let null pointers go; they're aligned.
   */
  if ((addr == 0) && (offset == 0))
    return;

  ++stat_poolcheck;
  PCLOCK();
  void* S = addr;
  unsigned len = 0;
  void * tag = 0;
  int t = adl_splay_retrieve(&MP->Objs, &S, &len, &tag);
  PCUNLOCK();
  if (t) {
    if (((addr - S) % size) == offset) {
      return;
    } else {
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(1): ", (unsigned)addr, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(2): ", (unsigned)S, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(3): ", (unsigned)offset, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(4): ", (unsigned)tag, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(5): ", (unsigned)size, (void*)__builtin_return_address(0));
      return;
    }
  }

  /*
   * Search through the set of function pointers.
   */
  PCLOCK2();
  t = adl_splay_retrieve(&MP->Functions, &S, &len, &tag);
  PCUNLOCK();

  if (t) {
    if (addr == S) {
      return;
    } else {
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(1): ", (unsigned)addr, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(2): ", (unsigned)S, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(3): ", (unsigned)offset, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign failure: Align(4): ", (unsigned)tag, (void*)__builtin_return_address(0));
      return;
    }
  }

  /*
   * The object has not been found.  Provide an error.
   */
  if (do_fail) poolcheckfail ("poolcheckalign failure: Missing(1): ", (unsigned)addr, (void*)__builtin_return_address(0));
  if (do_fail) poolcheckfail ("poolcheckalign failure: Missing(2): ", (unsigned)S, (void*)__builtin_return_address(0));
  if (do_fail) poolcheckfail ("poolcheckalign failure: Missing(3): ", (unsigned)offset, (void*)__builtin_return_address(0));
  if (do_fail) poolcheckfail ("poolcheckalign failure: Missing(4): ", (unsigned)tag, (void*)__builtin_return_address(0));
}

/*
 * Function: poolcheckalign_i()
 *
 * Description:
 *  This is the same as poolcheckalign(), but does not fail if an object cannot
 *  be found.  This is useful for checking incomplete/unknown nodes.
 *
 * Inputs:
 *  MP     - The metapool in which the object should be found.
 *  addr   - The pointer to the object which we seek.
 *  offset - The offset into the object at which the pointer should point.
 *  size   - The size of one object (in case this is an array).
 */
void
poolcheckalign_i (MetaPoolTy* MP, void* addr, unsigned offset, unsigned size) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;

  /*
   * Let null pointers go; they're aligned.
   */
  if ((addr == 0) && (offset == 0))
    return;

  PCLOCK();
  void* S = addr;
  unsigned len = 0;
  void * tag = 0;
  volatile int t = adl_splay_retrieve(&MP->Objs, &S, &len, &tag);
  PCUNLOCK();

  if (t) {
    if (((addr - S) % size) == offset) {
      return;
    } else {
      if (do_fail) poolcheckfail ("poolcheckalign_i: addr : ", (unsigned)addr, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: start: ", (unsigned)S, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: len  : ", (unsigned)len, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: offst: ", (unsigned)offset, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: tag  : ", (unsigned)tag, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: size : ", (unsigned)size, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i: roffs: ", (unsigned)((addr - S) % size), (void*)__builtin_return_address(0));
    }
  }

  /*
   * Search through the set of function pointers.
   */
  PCLOCK2();
  t = adl_splay_retrieve(&MP->Functions, &S, &len, &tag);
  PCUNLOCK();

  if (t) {
    if (addr == S) {
      return;
    } else {
      if (do_fail) poolcheckfail ("poolcheckalign_i failure: Align(1): ", (unsigned)addr, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i failure: Align(2): ", (unsigned)S, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i failure: Align(3): ", (unsigned)offset, (void*)__builtin_return_address(0));
      if (do_fail) poolcheckfail ("poolcheckalign_i failure: Align(4): ", (unsigned)tag, (void*)__builtin_return_address(0));
      return;
    }
  }

  /*
   * Ensure that the pointer is not within an I/O object.
   */
#ifdef SVA_IO
  PCLOCK2();
  if (adl_splay_find(&MP->IOObjs, addr)) {
    poolcheckfail ("poolcheck_i failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
  }
#endif

#ifdef SVA_KSTACKS
  /*
   * Ensure that the pointer is not within an Integer State object.
   */
  if (adl_splay_find (&(IntegerStatePool.Objs), addr)) {
    poolcheckfail ("poolcheck_i failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
  }
  PCUNLOCK();
#endif

  return;
}

/*
 * Function: poolcheck()
 *
 * Description:
 *  Check that addr exists in pool MP
 */
void *
poolcheck(MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return addr;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;
  PCLOCK();
  int t = adl_splay_find(&MP->Objs, addr);
  PCUNLOCK();
  if (t)
    return addr;
  if(do_fail) poolcheckfail ("poolcheck failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
  return 0;
}

/*
 * Function: poolcheck_i()
 *
 * Description:
 *  Same as poolcheck(), but does not fail if the pointer is not found. This is
 *  useful for checking incomplete/unknown nodes.
 */
void *
poolcheck_i (MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return addr;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;

  /*
   * Check the splay trees for a valid object.
   */
  PCLOCK();
  volatile int t = adl_splay_find(&MP->Objs, addr);
  PCUNLOCK();

  return addr;
}

/*
 * Function: poolcheckio()
 *
 * Description:
 *  Check that the given pointer is within the bounds of a valid I/O object.
 */
#ifdef SVA_IO
void *
poolcheckio (MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return addr;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheckio;

  /*
   * Determine if this is an I/O port address.  If so, just let it pass.
   */
  if (((unsigned int)(addr)) & 0xffff0000)
    return addr;

  PCLOCK();
  int t = adl_splay_find(&MP->IOObjs, addr);
  PCUNLOCK();
  if (t)
    return addr;
  poolcheckfail ("poolcheckio failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
  return 0;
}

void *
poolcheckio_i (MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return addr;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheckio;

  /*
   * Determine if this is an I/O port address.  If so, just let it pass.
   */
  if (((unsigned int)(addr)) & 0xffff0000)
    return addr;

  PCLOCK();
  int found = adl_splay_find(&MP->IOObjs, addr);

  if (found) {
    PCUNLOCK();
    return addr;
  }

  /*
   * If we didn't find the object, then try to ensure that the pointer is not
   * within a valid memory object.
   */
  found = adl_splay_find(&MP->IOObjs, addr);
  PCUNLOCK();

  if (found)
    poolcheckfail ("poolcheckio_i failure: ", (unsigned)addr, (void*)__builtin_return_address(0));

  /*
   * We don't know where this pointer can pointer to; just ignore it for now.
   */
  return addr;
}
#endif

/* check that src and dest are same obj or slab */
void poolcheckarray(MetaPoolTy* MP, void* src, void* dest) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheckarray;
  void* S = src;
  void* D = dest;
  PCLOCK();
  /* try objs */
  adl_splay_retrieve(&MP->Objs, &S, 0, 0);
  adl_splay_retrieve(&MP->Objs, &D, 0, 0);
  PCUNLOCK();
  if (S == D)
    return;
  if(do_fail) poolcheckfail ("poolcheck failure: ", (unsigned)src, (void*)__builtin_return_address(0));
}

/* check that src and dest are same obj or slab */
/* if src and dest do not exist in the pool, pass */
void poolcheckarray_i(MetaPoolTy* MP, void* src, void* dest) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheckarray_i;
  /* try slabs first */
  void* S = src;
  void* D = dest;
  PCLOCK();

  /* try objs */
  int fs = adl_splay_retrieve(&MP->Objs, &S, 0, 0);
  int fd = adl_splay_retrieve(&MP->Objs, &D, 0, 0);
  PCUNLOCK();
  if (S == D)
    return;
  if (fs || fd) { /*fail if we found one but not the other*/
    if(do_fail) poolcheckfail ("poolcheck failure: ", (unsigned)src, (void*)__builtin_return_address(0));
    return;
  }
  return; /*default is to pass*/
}

/*
 * Function: pchk_iccheck()
 *
 * Description:
 *  Determine whether the given pointer points to the beginning of an Interrupt
 *  Context.
 */
void
pchk_iccheck (void * addr) {
  if (!pchk_ready) return;

  /* try objs */
  void* S = addr;
  unsigned len = 0;
  PCLOCK();
  int fs = adl_splay_retrieve(&ICSplay, &S, &len, 0);
  PCUNLOCK();
  if (fs && (S == addr)) {
    return;
  }

  if (do_fail) poolcheckfail("iccheck failure: ", (unsigned) addr, (void*)__builtin_return_address(0));
  return;
}

const unsigned InvalidUpper = 4096;
const unsigned InvalidLower = 0x03;


/* if src is an out of object pointer, get the original value */
void* pchk_getActualValue(MetaPoolTy* MP, void* src) {
  if (!pchk_ready || !MP || !use_oob) return src;
  if ((unsigned)src <= InvalidLower) return src;
  void* tag = 0;
  /* outside rewrite zone */
  if ((unsigned)src & ~(InvalidUpper - 1)) return src;
  PCLOCK();
  if (adl_splay_retrieve(&MP->OOB, &src, 0, &tag)) {
    PCUNLOCK();
    return tag;
  }
  PCUNLOCK();
  if(do_fail) poolcheckfail("GetActualValue failure: ", (unsigned) src, (void*)__builtin_return_address(0));
  return tag;
}

/*
 * Function: getBounds()
 *
 * Description:
 *  Get the bounds associated with this object in the specified metapool.
 *
 * Return value:
 *  If the node is found in the pool, it returns the bounds relative to
 *  *src* (NOT the beginning of the object).
 *  If the node is not found in the pool, it returns 0x00000000.
 *  If the pool is not yet pchk_ready, it returns 0xffffffff
 */
#define USERSPACE 0xC0000000

struct node zero_page = {0, 0, 0, (char *)4095, 0};
struct node not_found = {0, 0, 0, (char *)0x00000000, 0};
struct node found =     {0, 0, 0, (char *)0xffffffff, 0};
struct node userspace = {0, 0, 0, (char* )USERSPACE, 0};

void * getBegin (void * node) {
  return ((struct node *)(node))->key;
}

void * getEnd (void * node) {
  return ((struct node *)(node))->end;
}

void* getBounds(MetaPoolTy* MP, void* src, void *dest) {
  if (!pchk_ready || !MP) return &found;

  /*
   * Update profiling and statistics.
   */
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_boundscheck;

  /*
   * First check for user space
   */
  if (src < USERSPACE) return &userspace;

  /* try objs */
  void* S = src;
  unsigned len = 0;
  unsigned tag = 0;
  PCLOCK();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  if (fs) {
    PCUNLOCK();
    return (MP->Objs);
  }

  /*
   * Try I/O objects.
   */
#ifdef SVA_IO
  /*
   * Get the bounds of the I/O object if possible.
   */
  S = src;
  len = 0;
  fs = adl_splay_retrieve(&MP->IOObjs, &S, &len, &tag);
  if (fs) {
    /*
     * Check the destination to see whether it falls within bounds.  If not,
     * it's possible that the value has been indexed into the physical address
     * associated with the I/O object.  In this case, return the bounds of that
     * object.
     */
    if ((!(S <= dest && ((char*)S + len) > (char*)dest)) && tag &&
       ((char *)tag <= dest && ((char*)tag + len) > (char*)dest)) {
      S = tag;
      if (adl_splay_retrieve(&MP->IOObjs, &S, &len, 0)) {
        PCUNLOCK();
        return (MP->IOObjs);
      } else {
        return &not_found;
      }
    }
    PCUNLOCK();
    return (MP->IOObjs);
  }
#endif

  PCUNLOCK();

  /*
   * If the source pointer is within the first page of memory, return the zero
   * page.
   */
  if (src < 4096)
    return &zero_page;

  /* Return that the object was not found */
  return &not_found;
}

/*
 * Function: getBounds_i()
 *
 * Description:
 *  Get the bounds associated with this object in the specified metapool.
 *
 * Return value:
 *  If the node is found in the pool, it returns the bounds.
 *  If the node is found within an integer state object, it returns 0x00000000.
 *  If the node is not found in the pool, it returns 0xffffffff.
 *  If the pool is not yet pchk_ready, it returns 0xffffffff
 */
void* getBounds_i(MetaPoolTy* MP, void* src, void *dest) {
  if (!pchk_ready || !MP) return &found;
  ++stat_boundscheck;
  /* Try fail cache first */
  PCLOCK();
#if 0
  int i = isInCache(MP, src);
  if (i) {
    mtfCache(MP, i);
    PCUNLOCK();
    return &found;
  }
#endif
#if 1
  {
    unsigned int index  = MP->cindex;
    unsigned int cindex = MP->cindex;
    do
    {
      if ((MP->start[index] <= src) &&
         (MP->start[index]+MP->length[index] > src)) {
        return MP->cache[index];
      }
      index = (index + 1) & 3;
    } while (index != cindex);
  }
#endif
  /* try objs */
  void* S = src;
  unsigned len = 0;
  unsigned tag = 0;
#if 0
  PCLOCK2();
#endif
#if 0
  long long tsc1, tsc2;
  if (do_profile) tsc1 = llva_save_tsc();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  if (do_profile) tsc2 = llva_save_tsc();
  if (do_profile) pchk_profile(MP, __builtin_return_address(0), (long)(tsc2 - tsc1));
#else
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
#endif

  if (fs) {
#if 1
    unsigned int index = MP->cindex;
    MP->start[index] = S;
    MP->length[index] = len;
    MP->cache[index] = MP->Objs;
    MP->cindex = (index+1) & 3u;
#endif
    PCUNLOCK();
    return MP->Objs;
  }

  /*
   * Try I/O objects.
   */
#ifdef SVA_IO
  S = src;
  len = 0;
  fs = adl_splay_retrieve(&MP->IOObjs, &S, &len, &tag);
  if (fs) {
    /*
     * Check the destination to see whether it falls within bounds.  If not,
     * it's possible that the value has been indexed into the physical address
     * associated with the I/O object.  In this case, return the bounds of that
     * object.
     */
    if (!((S <= dest) && ((char*)S + len) > (char*)dest)) {
      poolcheckinfo2("LLVA: Found I/O Object: 1: ", (int) dest, (int) S);
      poolcheckinfo2("LLVA: Found I/O Object: 2: ", (int) dest, (int) len);
      poolcheckinfo2("LLVA: Found I/O Object: 3: ", (int) dest, (int) tag);
      if (tag && ((((char *)tag) <= dest) && ((char*)tag + len) > (char*)dest)) {
        S = tag;
        if (adl_splay_retrieve(&MP->IOObjs, &S, &len, 0)) {
          PCUNLOCK();
          return (MP->IOObjs);
        } else {
          return &not_found;
        }
      }
    }
    PCUNLOCK();
    return (MP->IOObjs);
  }
#endif

#ifdef SVA_KSTACKS
  /*
   * Ensure that the destination pointer is not within the bounds of a saved
   * Integer State object.
   */
  S = src;
  len = 0;
  fs = adl_splay_retrieve (&(IntegerStatePool.Objs), &S, &len, 0);
  if (fs) {
    PCUNLOCK();
    return &not_found;
  }
  PCUNLOCK();
#endif

  /*
   * If the source pointer is within the first page of memory, return the zero
   * page.
   */
  if (src < 4096)
    return &zero_page;

  return &found;
}

/*
 * Function: getBoundsnoio_i()
 *
 * Description:
 *  Get the bounds associated with this object in the specified metapool.
 *  However, the object cannot be an I/O object, so do not bother with I/O
 *  object bounds.
 *
 * Return value:
 *  If the node is found in the pool, it returns the bounds.
 *  If the node is found within an integer state object, it returns 0x00000000.
 *  If the node is not found in the pool, it returns 0xffffffff.
 *  If the pool is not yet pchk_ready, it returns 0xffffffff
 */
void*
getBoundsnoio_i(MetaPoolTy* MP, void* src, void *dest) {
  if (!pchk_ready || !MP) return &found;
  ++stat_boundscheck;

  /* Try fail cache first */
  PCLOCK();
#if 0
  int i = isInCache(MP, src);
  if (i) {
    mtfCache(MP, i);
    PCUNLOCK();
    return &found;
  }
#endif

#if 0
  {
    unsigned int index  = MP->cindex;
    unsigned int cindex = MP->cindex;
    do
    {
      if ((MP->start[index] <= src) &&
         (MP->start[index]+MP->length[index] >= src))
        return MP->cache[index];
      index = (index + 1) & 3;
    } while (index != cindex);
  }
#endif

  /*
   * Look in the object splay for the given pointer.
   */
  void* S = src;
  unsigned len = 0;

  long long tsc1, tsc2;
#if 0
  if (do_profile) tsc1 = llva_save_tsc();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  if (do_profile) tsc2 = llva_save_tsc();
  if (do_profile) pchk_profile(MP, __builtin_return_address(0), (long)(tsc2 - tsc1));
#else
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
#endif

  if (fs) {
#if 1
    unsigned int index = MP->cindex;
    MP->start[index] = S;
    MP->length[index] = len;
    MP->cache[index] = MP->Objs;
    MP->cindex = (index+1) & 3u;
#endif
    PCUNLOCK();
    return MP->Objs;
  }

  /*
   * If the source pointer is within the first page of memory, return the zero
   * page.
   */
  if (src < 4096)
    return &zero_page;

  return &found;
}

char* invalidptr = 0;

/*
 * Function: boundscheck()
 *
 * Description:
 *  Perform a precise array bounds check on source and result.  If the result
 *  is out of range for the array, return 0x1 so that getactualvalue() will
 *  know that the pointer is bad and should not be dereferenced.
 */
void* pchk_bounds(MetaPoolTy* MP, void* src, void* dest) {
  if (!pchk_ready || !MP) return dest;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_boundscheck;
  /* try objs */
  void* S = src;
  unsigned len = 0;
  PCLOCK();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  PCUNLOCK();
  if ((fs) && S <= dest && ((char*)S + len) > (char*)dest )
    return dest;
  else if (fs) {
    if (!use_oob) {
      if(do_fail) poolcheckfail ("boundscheck failure 1", (unsigned)src, (void*)__builtin_return_address(0));
      return dest;
    }
    PCLOCK2();
    if (invalidptr == 0) invalidptr = (unsigned char*)InvalidLower;
    ++invalidptr;
    void* P = invalidptr;
    PCUNLOCK();
    if ((unsigned)P & ~(InvalidUpper - 1)) {
      if(do_fail) poolcheckfail("poolcheck failure: out of rewrite ptrs", 0, (void*)__builtin_return_address(0));
      return dest;
    }
    if(do_fail) poolcheckinfo2("Returning oob pointer of ", (int)P, __builtin_return_address(0));
    PCLOCK2();
    adl_splay_insert(&MP->OOB, P, 1, dest);
    PCUNLOCK()
    return P;
  }

  /*
   * The node is not found or is not within bounds; fail!
   */
  if(do_fail) poolcheckfail ("boundscheck failure 2", (unsigned)src, (void*)__builtin_return_address(0));
  return dest;
}

/*
 * Function: uiboundscheck()
 *
 * Description:
 *  Perform a precise array bounds check on source and result.  If the result
 *  is out of range for the array, return a sentinel so that getactualvalue()
 *  will know that the pointer is bad and should not be dereferenced.
 *
 *  This version differs from boundscheck() in that it does not generate a
 *  poolcheck failure if the source node cannot be found within the MetaPool.
 */
void* pchk_bounds_i(MetaPoolTy* MP, void* src, void* dest) {
  if (!pchk_ready || !MP) return dest;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_boundscheck_i;
  /* try fail cache */
  PCLOCK();
  int i = isInCache(MP, src);
  if (i) {
    mtfCache(MP, i);
    PCUNLOCK();
    return dest;
  }
  /* try objs */
  void* S = src;
  unsigned len = 0;
  unsigned int tag;
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, &tag);
  if ((fs) && (S <= dest) && (((unsigned char*)S + len) > (unsigned char*)dest)) {
    PCUNLOCK();
    return dest;
  }
  else if (fs) {
    if (!use_oob) {
      PCUNLOCK();
#if 0
      if(do_fail) poolcheckfail ("uiboundscheck failure 1", (unsigned)S, len);
      if(do_fail) poolcheckfail ("uiboundscheck failure 2", (unsigned)S, tag);
#endif
      if (do_fail) poolcheckfail ("uiboundscheck failure 3", (unsigned)dest, (void*)__builtin_return_address(0));
      return dest;
    }
     if (invalidptr == 0) invalidptr = (unsigned char*)0x03;
    ++invalidptr;
    void* P = invalidptr;
    if ((unsigned)P & ~(InvalidUpper - 1)) {
      PCUNLOCK();
      if(do_fail) poolcheckfail("poolcheck failure: out of rewrite ptrs", 0, (void*)__builtin_return_address(0));
      return dest;
    }
    adl_splay_insert(&MP->OOB, P, 1, dest);
    PCUNLOCK();
    return P;
  }

  /*
   * The node is not found or is not within bounds; pass!
   */
  int nn = insertCache(MP, src);
  mtfCache(MP, nn);
  PCUNLOCK();
  return dest;
}

void funccheck_g (MetaPoolTy * MP, void * f) {
  void* S = f;
  unsigned len = 0;

  PCLOCK();
  int fs = adl_splay_retrieve(&MP->Functions, &S, &len, 0);
  PCUNLOCK();
  if (fs)
    return;

  if (do_fail) poolcheckfail ("funccheck_g failed", f, (void*)__builtin_return_address(0));
}

void pchk_ind_fail(void * f) {
  if (do_fail) poolcheckfail("indirect call failure", f, (void*)__builtin_return_address(0));
}


void __sva_pool_init(void * Pool, int NodeSize, int align) {
  // Dummy initialization function
}

