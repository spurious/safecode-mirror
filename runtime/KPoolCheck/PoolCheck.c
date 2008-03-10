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

/* Flag whether we are pchk_ready to perform pool operations */
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

/* Global splay for holding the interrupt context */
void * ICSplay;

/* Global splay for holding the integer states */
MetaPoolTy IntegerStatePool;

extern void llva_load_lif (unsigned int enable);
extern unsigned int llva_save_lif (void);


static unsigned
disable_irqs ()
{
  unsigned int is_set;
  is_set = llva_save_lif ();
  llva_load_lif (0);
  return is_set;
}

static void
enable_irqs (int is_set)
{
  llva_load_lif (is_set);
}

#define PCLOCK() int pc_i = disable_irqs();
#define PCLOCK2() pc_i = disable_irqs();
#define PCUNLOCK() enable_irqs(pc_i);

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
void pchk_init(void) {

  /* initialize runtime */
  adl_splay_libinit(poolcheckmalloc);

  /*
   * Register all of the global variables in their respective meta pools.
   */
  poolcheckglobals();

  /*
   * Flag that we're pchk_ready to rumble!
   */
  pchk_ready = 1;
  return;
}

/* Register a slab */
void pchk_reg_slab(MetaPoolTy* MP, void* PoolID, void* addr, unsigned len) {
#if 0
  if (!MP) { poolcheckinfo("reg slab on null pool", (int)addr); return; }
#else
  if (!MP) { return; }
#endif
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

/* Register a non-pool allocated object */
void pchk_reg_obj(MetaPoolTy* MP, void* addr, unsigned len) {
  unsigned int index;
#if 0
  if (!MP) { poolcheckinfo("reg obj on null pool", addr); return; }
#else
  if (!MP) { return; }
#endif
#if 0
  if (pchk_ready) poolcheckinfo2 ("pchk_reg_obj", addr, len);
#endif
  PCLOCK();
#if 0
  {
  void * S = addr;
  unsigned len, tag = 0;
  if ((pchk_ready) && (adl_splay_retrieve(&MP->Objs, &S, &len, &tag)))
    poolcheckinfo2 ("regobj: Object exists", __builtin_return_address(0), tag);
  }
#endif

  adl_splay_insert(&MP->Objs, addr, len, __builtin_return_address(0));
#if 1
  /*
   * Look for an entry in the cache that matches.  If it does, just erase it.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] >= addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
#endif
  PCUNLOCK();
}

void pchk_reg_stack (MetaPoolTy* MP, void* addr, unsigned len) {
  unsigned int index;
  if (!MP) { return; }
  PCLOCK();

  adl_splay_insert(&MP->Objs, addr, len, __builtin_return_address(0));
#if 1
  /*
   * Look for an entry in the cache that matches.  If it does, just erase it.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] >= addr)) {
      MP->start[index] = 0;
      MP->length[index] = 0;
      MP->cache[index] = 0;
    }
  }
#endif
  PCUNLOCK();
}

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

void pchk_reg_int (void* addr) {
  unsigned int index;
  if (!pchk_ready) return;
  PCLOCK();
  adl_splay_insert(&(IntegerStatePool.Objs), addr, 72, __builtin_return_address(0));
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

/* Remove a non-pool allocated object */
void pchk_drop_obj(MetaPoolTy* MP, void* addr) {
  unsigned int index;
  if (!MP) return;
  PCLOCK();
  adl_splay_delete(&MP->Objs, addr);
#if 0
  {
  void * S = addr;
  unsigned len, tag;
  if (adl_splay_retrieve(&MP->Objs, &S, &len, &tag))
    poolcheckinfo ("drop_obj: Failed to remove: 1", addr, tag);
  }
#endif
  /*
   * See if the object is within the cache.  If so, remove it from the cache.
   */
  for (index=0; index < 4; ++index) {
    if ((MP->start[index] <= addr) &&
       (MP->start[index]+MP->length[index] >= addr)) {
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
       (MP->start[index]+MP->length[index] >= addr)) {
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
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inference bug a): ", (unsigned)*(void**)MPLoc, (void*)__builtin_return_address(0));
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inference bug b): ", (unsigned) MP, (void*)__builtin_return_address(0));
    if(do_fail) poolcheckfail("reg_pool: Pool in 2 MP (inference bug c): ", (unsigned) PoolID, (void*)__builtin_return_address(0));
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
poolcheckalign (MetaPoolTy* MP, void* addr, unsigned offset) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;
  PCLOCK();
  void* S = addr;
  unsigned len = 0;
  int t = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  PCUNLOCK();
  if ((t) && ((addr - S) == offset))
    return;
  if(do_fail) poolcheckfail ("poolcheckalign failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
}

/*
 * Function: poolcheckalign_i()
 *
 * Description:
 *  This is the same as poolcheckalign(), but does not fail if an object cannot
 *  be found.  This is useful for checking incomplete/unknown nodes.
 */
void
poolcheckalign_i (MetaPoolTy* MP, void* addr, unsigned offset) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;
  PCLOCK();
  void* S = addr;
  unsigned len = 0;
  volatile int t = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  PCUNLOCK();
  if (t) {
    if ((addr - S) == offset)
      return;
    else
      if (do_fail) poolcheckfail ("poolcheckalign_i failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
  }
  return;
}

/* check that addr exists in pool MP */
void poolcheck(MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;
  PCLOCK();
  int t = adl_splay_find(&MP->Objs, addr);
  PCUNLOCK();
  if (t)
    return;
  if(do_fail) poolcheckfail ("poolcheck failure: ", (unsigned)addr, (void*)__builtin_return_address(0));
}

/*
 * Function: poolcheck_i()
 *
 * Description:
 *  Same as poolcheck(), but does not fail if the pointer is not found. This is
 *  useful for checking incomplete/unknown nodes.
 */
void poolcheck_i (MetaPoolTy* MP, void* addr) {
  if (!pchk_ready || !MP) return;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_poolcheck;
  PCLOCK();
  volatile int t = adl_splay_find(&MP->Objs, addr);
  PCUNLOCK();
  return;
}

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
struct node {
  void* left;
  void* right;
  char* key;
  char* end;
  void* tag;
};

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

void* getBounds(MetaPoolTy* MP, void* src) {
  if (!pchk_ready || !MP) return &found;
#if 0
  if (do_profile) pchk_profile(MP, __builtin_return_address(0));
#endif
  ++stat_boundscheck;
  /* first check for user space */
  if (src < USERSPACE) return &userspace;

  /* try objs */
  void* S = src;
  unsigned len = 0;
  PCLOCK();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  if (fs) {
    PCUNLOCK();
    return (MP->Objs);
  }

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
 *  If the node is not found in the pool, it returns 0xffffffff.
 *  If the pool is not yet pchk_ready, it returns 0xffffffff
 */
void* getBounds_i(MetaPoolTy* MP, void* src) {
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
         (MP->start[index]+MP->length[index] >= src))
        return MP->cache[index];
      index = (index + 1) & 3;
    } while (index != cindex);
  }
#endif
  /* try objs */
  void* S = src;
  unsigned len = 0;
#if 0
  PCLOCK2();
#endif
  long long tsc1, tsc2;
  if (do_profile) tsc1 = llva_save_tsc();
  int fs = adl_splay_retrieve(&MP->Objs, &S, &len, 0);
  if (do_profile) tsc2 = llva_save_tsc();
  if (do_profile) pchk_profile(MP, __builtin_return_address(0), (long)(tsc2 - tsc1));
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

  PCUNLOCK();

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

