/* Profiling support:

   Each Metapool contains a pointer to a profile tree.  this tree
   tracks profile call site and frequency A global tree is also
   maintained that tracks all metapools.
*/

#include "PoolCheck.h"
#include "adl_splay.h"

extern int printk(const char *fmt, ...);

static void* allmp = 0;
int profile_pause = 1;

void pchk_profile(MetaPoolTy* MP, void* pc, long time) {
  if (profile_pause) return;
  if (!MP) return;

  if (!adl_splay_retrieve (&allmp, MP, 0, 0))
    adl_splay_insert(&allmp, MP, 1, 0);

  void* key = pc;
  unsigned tag=0;
  unsigned len=0;
  
  if (adl_splay_retrieve(&MP->profile, &key, &len, &tag)) {
    tag += time;
    adl_splay_insert(&MP->profile, key, len, tag);
  } else {
    adl_splay_insert(&MP->profile, key, 1, tag);
  }
}

static void * thepool;

void print_item(void* p, unsigned l, void* t) {
  poolcheckinfo2 ("item1: ", thepool, p);
  poolcheckinfo2 ("item2: ", thepool, (unsigned) t);
}

void print_pool(void* p, unsigned l, void* t) {
  thepool = p;
  adl_splay_foreach(&(((MetaPoolTy*)p)->profile), print_item);
}

void pchk_profile_print() {
  int old = profile_pause;
  profile_pause = 1;

  poolcheckinfo ("LLVA:Printing Profile:\n", 0);
  adl_splay_foreach(&allmp, print_pool);
  
  profile_pause = old;  
}

