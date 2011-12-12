//=== SoftBoundRuntime/softboundcets.c - Creates the main function for SoftBound+CETS Runtime --*- C -*===// 
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


#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <execinfo.h>
#include "softboundcets.h"

__softboundcets_trie_entry_t** __softboundcets_trie_primary_table;
//__softboundcets_trie_entry_t* __softboundcets_trie_primary_table[__SOFTBOUNDCETS_TRIE_PRIMARY_TABLE_ENTRIES] = {NULL};


size_t* __softboundcets_free_map_table = NULL;

size_t* __softboundcets_shadow_stack_ptr = NULL;

size_t* __softboundcets_lock_next_location = NULL;
size_t* __softboundcets_lock_new_location = NULL;
size_t __softboundcets_key_id_counter = 2;

size_t __softboundcets_statistics_load_dereference_checks = 0;
size_t __softboundcets_statistics_store_dereference_checks = 0;
size_t __softboundcets_statistics_temporal_load_dereference_checks = 0;
size_t __softboundcets_statistics_temporal_store_dereference_checks = 0;
size_t __softboundcets_statistics_metadata_loads = 0;
size_t __softboundcets_statistics_metadata_stores = 0;

/* key 0 means not used, 1 means globals*/
size_t __softboundcets_deref_check_count = 0;
size_t* __softboundcets_global_lock = 0;

size_t* __softboundcets_temporal_space_begin = 0;
size_t* __softboundcets_stack_temporal_space_begin = NULL;

void* malloc_address = NULL;

__SOFTBOUNDCETS_NORETURN void __softboundcets_abort()
{
  fprintf(stderr, "\nSoftboundcets: Bounds violation detected\n\nBacktrace:\n");

  // Based on code from the backtrace man page
  size_t size;
  void *array[100];
  
  size = backtrace(array, 100);
  backtrace_symbols_fd(array, size, fileno(stderr));
  
  fprintf(stderr, "\n\n");

  abort();
}

static int softboundcets_initialized = 0;

__NO_INLINE void __softboundcets_stub(void) {
  return;
}
void __softboundcets_init( int is_trie) 
{
  if (__SOFTBOUNDCETS_DEBUG) {
    __softboundcets_printf("Running __softboundcets_init for module\n");
  }
  
  if (is_trie != __SOFTBOUNDCETS_TRIE) {
    __softboundcets_printf("Softboundcets: Inconsistent specification of metadata encoding\n");
    abort();
  }

  if (softboundcets_initialized != 0) {
    return;  // already initialized, do nothing
  }
  
  softboundcets_initialized = 1;

  if (__SOFTBOUNDCETS_DEBUG) {
    __softboundcets_printf("Initializing softboundcets metadata space\n");
  }

  if(__SOFTBOUNDCETS_TRIE){
    assert(sizeof(__softboundcets_trie_entry_t) >= 16);
  }


  /* Allocating the temporal shadow space */

  size_t temporal_table_length = (__SOFTBOUNDCETS_N_TEMPORAL_ENTRIES)* sizeof(void*);

  __softboundcets_lock_new_location = mmap(0, temporal_table_length, PROT_READ| PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(__softboundcets_lock_new_location != (void*) -1);
  __softboundcets_temporal_space_begin = (size_t *)__softboundcets_lock_new_location;
  //  printf("temp table %lx %lx\n", __softboundcets_lock_new_location, temporal_table_length);


  size_t stack_temporal_table_length = (__SOFTBOUNDCETS_N_STACK_TEMPORAL_ENTRIES) * sizeof(void*);
  __softboundcets_stack_temporal_space_begin = mmap(0, stack_temporal_table_length, PROT_READ| PROT_WRITE, MAP_PRIVATE| MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(__softboundcets_stack_temporal_space_begin != (void*) -1);
  //  printf("temp stack table %p %zx\n", __softboundcets_stack_temporal_space_begin, stack_temporal_table_length);


  size_t global_lock_size = (__SOFTBOUNDCETS_N_GLOBAL_LOCK_SIZE) * sizeof(void*);
  __softboundcets_global_lock = mmap(0, global_lock_size, PROT_READ|PROT_WRITE, MAP_PRIVATE| MAP_ANONYMOUS| MAP_NORESERVE, -1, 0);
  assert(__softboundcets_global_lock != (void*) -1);
  //  __softboundcets_global_lock =  __softboundcets_lock_new_location++;
  *((size_t*)__softboundcets_global_lock) = 1;



  size_t shadow_stack_size = __SOFTBOUNDCETS_SHADOW_STACK_ENTRIES * sizeof(size_t);
  __softboundcets_shadow_stack_ptr = mmap(0, shadow_stack_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(__softboundcets_shadow_stack_ptr != (void*)-1);

  *((size_t*)__softboundcets_shadow_stack_ptr) = 0; /* prev stack size */
  size_t * current_size_shadow_stack_ptr =  __softboundcets_shadow_stack_ptr +1 ;
  *(current_size_shadow_stack_ptr) = 0;

  if(__SOFTBOUNDCETS_SHADOW_STACK_DEBUG){
    printf("[mmap_shadow_stack]mmaped shadowstack pointer = %p\n", __softboundcets_shadow_stack_ptr);
  }

  if(__SOFTBOUNDCETS_FREE_MAP) {
    size_t length_free_map = (__SOFTBOUNDCETS_N_FREE_MAP_ENTRIES) * sizeof(size_t);
    __softboundcets_free_map_table = mmap(0, length_free_map, PROT_READ| PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    assert(__softboundcets_free_map_table != (void*) -1);
  }

  if(__SOFTBOUNDCETS_TRIE) {
    size_t length_trie = (__SOFTBOUNDCETS_TRIE_PRIMARY_TABLE_ENTRIES) * sizeof(__softboundcets_trie_entry_t*);

    __softboundcets_trie_primary_table = mmap(0, length_trie, PROT_READ| PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    assert(__softboundcets_trie_primary_table != (void *)-1);  // FIXME - don't use assert, always want this to fail
    
    int* temp = malloc(1);
    __softboundcets_allocation_secondary_trie_allocate_range(0, (size_t)temp);
    
#ifdef __SOFTBOUNDCETS_TRIE_ROOT_PTR_REGISTER
    //    printf("The value of trie_root is %p\n", __softboundcets_trie_primary_table);
    __asm__("icrtst %0, %0\n\t"
            :
            :"r"(__softboundcets_trie_primary_table)
            );
#endif
   

    return;
  }


}

static void softboundcets_init_ctype(){  
  char* ptr;
  char* base_ptr;

  ptr = (void*) __ctype_b_loc();
  base_ptr = (void*) (*(__ctype_b_loc()));
  __softboundcets_allocation_secondary_trie_allocate(base_ptr);

#ifdef __SOFTBOUNDCETS_SPATIAL
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), ((char*) base_ptr + 256));

#elif __SOFTBOUNDCETS_TEMPORAL
  __softboundcets_metadata_store(ptr, 1, __softboundcets_global_lock);

#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), ((char*) base_ptr + 256), 1, __softboundcets_global_lock);

#else  
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), ((char*) base_ptr + 256), 1, __softboundcets_global_lock);
  
#endif
}


void __softboundcets_printf(const char* str, ...)
{
  va_list args;
  
  va_start(args, str);
  vfprintf(stderr, str, args);
  va_end(args);
}

#ifdef __SOFTBOUNDCETS_XMM_MODE
extern int softboundcets_pseudo_main(int, char**, __v2di, __v2di);
#else
extern int softboundcets_pseudo_main(int argc, char **argv);
#endif

int main(int argc, char **argv){

  char** new_argv = argv;
  int i;
  char* temp_ptr;
  int return_value;
  size_t argv_key;
  void* argv_loc;

  int* temp = malloc(1);
  malloc_address = temp;
  __softboundcets_allocation_secondary_trie_allocate_range(0, (size_t)temp);

  __softboundcets_stack_memory_allocation(argv, &argv_loc, &argv_key);

  mallopt(M_MMAP_MAX, 0);
  for(i = 0; i < argc; i++) { 

#ifdef __SOFTBOUNDCETS_SPATIAL

    __softboundcets_metadata_store(&new_argv[i], new_argv[i], new_argv[i] + strlen(new_argv[i]) + 1);
    
#elif __SOFTBOUNDCETS_TEMPORAL
    //    printf("performing metadata store\n");
    __softboundcets_metadata_store(&new_argv[i],  argv_key, argv_loc);
    
#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL

    __softboundcets_metadata_store(&new_argv[i], new_argv[i], new_argv[i] + strlen(new_argv[i]) + 1, argv_key, argv_loc);

#else

    __softboundcets_metadata_store(&new_argv[i], new_argv[i], new_argv[i] + strlen(new_argv[i]) + 1, argv_key, argv_loc);

#endif


  }

  //  printf("before init_ctype\n");
  softboundcets_init_ctype();

  /* Santosh: Real Nasty hack because C programmers assume argv[argc]
   * to be NULL. Also this NUll is a pointer, doing + 1 will make the
   * size_of_type to fail 
   */
  temp_ptr = ((char*) &new_argv[argc]) + 8;

  /* &new_argv[0], temp_ptr, argv_key, argv_loc * the metadata */

  __softboundcets_allocate_shadow_stack_space(2);

#ifdef __SOFTBOUNDCETS_SPATIAL

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);

#elif __SOFTBOUNDCETS_TEMPORAL

  //  printf("before writing to shadow stack\n");
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#else

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#endif
  
  //  printf("before calling program main\n");
  return_value = softboundcets_pseudo_main(argc, new_argv);
  __softboundcets_deallocate_shadow_stack_space();


  return return_value;
}

void * safe_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset){
  return mmap(addr, length, prot, flags, fd, offset);
}

void* safe_calloc(size_t nmemb, size_t size){

  return calloc(nmemb, size);
}

void* safe_malloc(size_t size){

  return malloc(size);
}
void safe_free(void* ptr){

  free(ptr);
}
