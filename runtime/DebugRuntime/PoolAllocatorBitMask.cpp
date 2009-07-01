//===- PoolAllocatorBitMask.cpp - Implementation of poolallocator runtime -===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file is one possible implementation of the LLVM pool allocator runtime
// library.
//
// This uses the 'Ptr1' field to maintain a linked list of slabs that are either
// empty or are partially allocated from.  The 'Ptr2' field of the PoolTy is
// used to track a linked list of slabs which are full, ie, all elements have
// been allocated from them.
//
//===----------------------------------------------------------------------===//
// NOTES:
//  1) Some of the bounds checking code may appear strange.  The reason is that
//     it is manually inlined to squeeze out some more performance.  Please
//     don't change it.
//
//  2) This run-time performs MMU re-mapping of pages to perform dangling
//     pointer detection.  A "shadow" address is the address of a memory block
//     that has been remapped to a new virtal address; the shadow address is
//     returned to the caller on allocation and is unmapped on deallocation.
//     A "canonical" address is the virtual address of memory as it is mapped
//     in the pool slabs; the canonical address is remapped to different shadow
//     addresses each time that particular piece of memory is allocated.
//
//     In normal operation, the shadow address and canonical address are
//     identical.
//
//===----------------------------------------------------------------------===//

#include "SafeCodeRuntime.h"
#include "ConfigData.h"
#include "PoolAllocator.h"
#include "PageManager.h"
#include "Report.h"

#include <cstring>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#if 0
#include <sys/ucontext.h>
#endif

#include <pthread.h>

#define DEBUG(x)

NAMESPACE_SC_BEGIN

DebugPoolTy dummyPool;

// Structure defining configuration data
struct ConfigData ConfigData = {false, true, false};

// Invalid address range
#if !defined(__linux__)
unsigned InvalidUpper = 0x00000000;
unsigned InvalidLower = 0x00000003;
#endif

NAMESPACE_SC_END

using namespace NAMESPACE_SC;

// global variable declarations
static unsigned globalallocID = 0;
static unsigned globalfreeID = 0;

/// UNUSED in production version
FILE * ReportLog = 0;

// Configuration for C code; flags that we should stop on the first error
unsigned StopOnError = 0;

// signal handler
static void bus_error_handler(int, siginfo_t *, void *);

// creates a new PtrMetaData structure to record pointer information
static inline void updatePtrMetaData(PDebugMetaData, unsigned, void *);
static PDebugMetaData createPtrMetaData (unsigned,
                                         unsigned,
                                         void *,
                                         void *,
                                         void *,
                                         char * SourceFile = "<unknown>",
                                         unsigned lineno = 0);

//===----------------------------------------------------------------------===//
//
//  Pool allocator library implementation
//
//===----------------------------------------------------------------------===//


//
// Function: pool_init_runtime()
//
// Description:
//  This function is called to initialize the entire SAFECode run-time.  It
//  configures the various run-time options for SAFECode and performs other
//  initialization tasks.
//
// Inputs:
//  Dangling   - Set to non-zero to enable dangling pointer detection.
//  RewriteOOB - Set to non-zero to enable Out-Of-Bounds pointer rewriting.
//  Termiante  - Set to non-zero to have SAFECode terminate when an error
//               occurs.
//

extern "C" char __poolalloc_GlobalPool[sizeof (DebugPoolTy)];
void
pool_init_runtime (unsigned Dangling, unsigned RewriteOOB, unsigned Terminate) {
  //
  // Configure the global pool.
  //
  // Call the in-place constructor for the splay tree of objects and, if
  // applicable, the set of Out of Bound rewrite pointers and the splay tree
  // used for dangling pointer detection.
  //
  // FIXME: This is rediculous. Just a temporary workaround.
  // deally, it should be done by allocating the pool on
  // the heap in pool allocation, then SAFECode redirects the calls to a
  // customized constructor.
  DebugPoolTy * GlobalPool = reinterpret_cast<DebugPoolTy*>(&__poolalloc_GlobalPool);
  new (&(GlobalPool->Objects)) RangeSplaySet<>();
#if SC_ENABLE_OOB 
  new (&(GlobalPool->OOB)) RangeSplayMap<void *>();
#endif
#if SC_DEBUGTOOL
  new (&(GlobalPool->DPTree)) RangeSplayMap<PDebugMetaData>();
#endif

  //
  // Initialize the Global Pool
  //
  __pa_bitmap_poolinit(GlobalPool, 1);

  //
  // Initialize the signal handlers for catching errors.
  ConfigData.RemapObjects = Dangling;
  ConfigData.StrictIndexing = !(RewriteOOB);
  StopOnError = Terminate;

  //
  // Allocate a range of memory for rewrite pointers.
  //
#if !defined(__linux__)
  const unsigned invalidsize = 1 * 1024 * 1024 * 1024;
  void * Addr = mmap (0, invalidsize, 0, MAP_SHARED | MAP_ANON, -1, 0);
  if (Addr == MAP_FAILED) {
     perror ("mmap:");
     fflush (stdout);
     fflush (stderr);
     assert(0 && "valloc failed\n");
  }
  //memset (Addr, 0x00, invalidsize);
  madvise (Addr, invalidsize, MADV_FREE);
  InvalidLower = (unsigned int) Addr;
  InvalidUpper = (unsigned int) Addr + invalidsize;
#endif

  //
  // Leave initialization of the Report logfile to the reporting routines.
  // The libc stdio functions may have not been initialized by this point, so
  // we cannot rely upon them working.
  //
  ReportLog = stderr;

  //
  // Install hooks for catching allocations outside the scope of SAFECode.
  //
  if (ConfigData.TrackExternalMallocs) {
    extern void installAllocHooks(void);
    installAllocHooks();
  }

#if SC_DEBUGTOOL  
  //
  // Initialize the dummy pool.
  //
  __pa_bitmap_poolinit(static_cast<BitmapPoolTy*>(&dummyPool), 1);

  //
  // Initialize the signal handlers for catching errors.
  //
  struct sigaction sa;
  bzero (&sa, sizeof (struct sigaction));
  sa.sa_sigaction = bus_error_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGBUS, &sa, NULL) == -1) {
    fprintf (stderr, "sigaction installer failed!");
    fflush (stderr);
  }
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    fprintf (stderr, "sigaction installer failed!");
    fflush (stderr);
  }
#endif

  return;
}

//
// Function: __sc_dbg_newpool()
//
// Description:
//  Retuen a pool descriptor for a new pool.
//
void *
__sc_dbg_newpool(unsigned NodeSize) {
  DebugPoolTy * Pool = new DebugPoolTy();
  __pa_bitmap_poolinit(static_cast<BitmapPoolTy*>(Pool), NodeSize);
  return Pool;
}

// pooldestroy - Release all memory allocated for a pool
//
// FIXME: determine how to adjust debug logs when 
//        pooldestroy is called
void
__sc_dbg_pooldestroy(DebugPoolTy * Pool) {
  assert(Pool && "Null pool pointer passed in to pooldestroy!\n");

  Pool->Objects.clear();
  Pool->OOB.clear();
  Pool->DPTree.clear();
  __pa_bitmap_pooldestroy(Pool);
  delete Pool;
}

//
// Function: poolargvregister()
//
// Description:
//  Register all of the argv strings in the external object pool.
//
void
__sc_dbg_poolargvregister (int argc, char ** argv) {
  for (int index=0; index < argc; ++index) {
    if (logregs) {
      fprintf (stderr, "poolargvregister: %p %u: %s\n", argv[index], strlen(argv[index]), argv[index]);
      fflush (stderr);
    }
    ExternalObjects.insert(argv[index], argv[index] + strlen (argv[index]));
  }

  return;
}

//
// Function: poolregister_debug()
//
// Description:
//  Register the memory starting at the specified pointer of the specified size
//  with the given Pool.  This version will also record debug information about
//  the object being registered.
//
void
__sc_dbg_src_poolregister (DebugPoolTy *Pool,
                    void * allocaptr,
                    unsigned NumBytes,
                    const char * SourceFilep,
                    unsigned lineno) {
  //
  // If the pool is NULL or the object has zero length, don't do anything.
  //
  if ((!Pool) || (NumBytes == 0)) return;

  //
  // Add the object to the pool's splay of valid objects.
  //
  Pool->Objects.insert(allocaptr, (char*) allocaptr + NumBytes - 1);

  // Do some initial casting for type goodness
  char * SourceFile = (char *)(SourceFilep);

  //
  // Create the meta data object containing the debug information for this
  // pointer.  These pointers will never be shadowed, but we want to record
  // information about the allocation in case a bounds check on this object
  // fails.
  //
  PDebugMetaData debugmetadataPtr;
  globalallocID++;
  debugmetadataPtr = createPtrMetaData (globalallocID,
                                        globalfreeID,
                                        __builtin_return_address(0),
                                        0,
                                        allocaptr, SourceFile, lineno);
  dummyPool.DPTree.insert (allocaptr,
                           (char*) allocaptr + NumBytes - 1,
                           debugmetadataPtr);

  //
  // Call the real poolregister() function to register the object.
  //
  if (logregs) {
    fprintf (ReportLog, "poolregister_debug: %p: %p %d: %s %d\n", 
             (void*) Pool, (void*)allocaptr, NumBytes, SourceFile, lineno);
    fflush (ReportLog);
  }
}

//
// Function: poolregister()
//
// Description:
//  Register the memory starting at the specified pointer of the specified size
//  with the given Pool.  This version will also record debug information about
//  the object being registered.
//
void
__sc_dbg_poolregister (DebugPoolTy *Pool, void * allocaptr,
                           unsigned NumBytes) {
  __sc_dbg_src_poolregister (Pool, allocaptr, NumBytes, "<unknown>", 0);
}

//
// Function: poolunregister()
//
// Description:
//  Remove the specified object from the set of valid objects in the Pool.
//
// Inputs:
//  Pool      - The pool in which the object should belong.
//  allocaptr - A pointer to the object to remove.
//
// Notes:
//  Note that this function currently deallocates debug information about the
//  allocation.  This is safe because this function is only called on stack
//  objects.  This is less-than-ideal because we lose debug information about
//  the allocation of the stack object if it is later dereferenced outside its
//  function (dangling pointer), but it is currently too expensive to keep that
//  much debug information around.
//
//  TODO: What are the restrictions on allocaptr?
//
void
__sc_dbg_poolunregister(DebugPoolTy *Pool, void * allocaptr) {
  //
  // If no pool was specified, then do nothing.
  //
  if (!Pool) return;

  //
  // Remove the object from the pool's splay tree.
  //
  Pool->Objects.remove (allocaptr);

  // Canonical pointer for the pointer we're freeing
  void * CanonNode = allocaptr;

  //
  // Increment the ID number for this deallocation.
  //
  globalfreeID++;

  // The start and end of the object as registered in the dangling pointer
  // object metapool
  void * start, * end;

  // FIXME: figure what NumPPAge and len are for
  unsigned len = 1;
  unsigned NumPPage = 0;
  unsigned offset = (unsigned)((long)allocaptr & (PPageSize - 1));
  PDebugMetaData debugmetadataptr = 0;
  
  //
  // Retrieve the debug information about the node.  This will include a
  // pointer to the canonical page.
  //
  bool found = dummyPool.DPTree.find (allocaptr, start, end, debugmetadataptr);

  //
  // If we cannot find the meta-data for this pointer, then the free is
  // invalid.  Report it as an error and then continue executing if possible.
  //
  if (!found) {
    ReportInvalidFree ((unsigned)__builtin_return_address(0),
                       allocaptr,
                       "<Unknown>",
                       0);
    return;
  }

  // Assert that we either didn't find the object or we found the object *and*
  // it has meta-data associated with it.
  assert ((!found || (found && debugmetadataptr)) &&
          "poolfree: No debugmetadataptr\n");

  if (logregs) {
    fprintf(stderr, "poolfree:1387: start = 0x%08x, end = 0x%x,  offset = 0x%08x\n", (unsigned)start, (unsigned)(end), offset);
    fprintf(stderr, "poolfree:1388: len = %d\n", len);
    fflush (stderr);
  }

  //
  // If dangling pointer detection is not enabled, remove the object from the
  // dangling pointer splay tree.  The memory object's memory will be reused,
  // and we don't want to match it for subsequently allocated objects.
  //
  if (!(ConfigData.RemapObjects)) {
    free (debugmetadataptr);
    dummyPool.DPTree.remove (allocaptr);
  }

  // figure out how many pages does this object span to
  //  protect the pages. First we sum the offset and len
  //  to get the total size we originally remapped.
  //  Then, we determine if this sum is a multiple of
  //  physical page size. If it is not, then we increment
  //  the number of pages to protect.
  //  FIXME!!!
  NumPPage = (len / PPageSize) + 1;
  if ( (len - (NumPPage-1) * PPageSize) > (PPageSize - offset) )
    NumPPage++;

  //
  // If this is a remapped pointer, find its canonical address.
  //
  if (ConfigData.RemapObjects) {
    CanonNode = debugmetadataptr->canonAddr;
    updatePtrMetaData (debugmetadataptr,
                       globalfreeID,
                       __builtin_return_address(0));
  }

  if (logregs) {
    fprintf(stderr, " poolfree:1397: NumPPage = %d\n", NumPPage);
    fprintf(stderr, " poolfree:1398: canonical address is 0x%x\n", (unsigned)CanonNode);
    fflush (stderr);
  }

  if (logregs) {
    fprintf (stderr, "pooluregister: %p\n", allocaptr);
  }
}

//
// Function: poolalloc_debug()
//
// Description:
//  This function is just like poolalloc() except that it associates a source
//  file and line number with the allocation.
//
void *
__sc_dbg_src_poolalloc (DebugPoolTy *Pool,
                 unsigned NumBytes,
                 const char * SourceFilep,
                 unsigned lineno) {
  //
  // Ensure that we're allocating at least one byte.
  //
  if (NumBytes == 0) NumBytes = 1;

  // Perform the allocation and determine its offset within the physical page.
  void * canonptr = __pa_bitmap_poolalloc(Pool, NumBytes);
  uintptr_t offset = (((uintptr_t)(canonptr)) & (PPageSize-1));

  // Remap the object if necessary.
  void * shadowpage = RemapObject (canonptr, NumBytes);
  void * shadowptr = (unsigned char *)(shadowpage) + offset;

  // Return the shadow pointer.
  return shadowptr;
}

//
// Function: poolfree_debug()
//
// Description:
//  This function is identical to poolfree() except that it relays source-level
//  debug information to the error reporting routines.
//
void
__sc_dbg_src_poolfree (DebugPoolTy *Pool,
                void * Node,
                const char * SourceFile,
                unsigned lineno) {
  //
  // Free the object within the pool; the poolunregister() function will
  // detect invalid frees.
  //
  __pa_bitmap_poolfree (Pool, Node);
}


//===----------------------------------------------------------------------===//
//
// Dangling pointer runtime functions
//
//===----------------------------------------------------------------------===//

//
// Function: createPtrMetaData()
//  Allocates memory for a DebugMetaData struct and fills up the appropriate
//  fields so to keep a record of the pointer's meta data
//
// Inputs:
//  AllocID - A unique identifier for the allocation.
//  FreeID  - A unique identifier for the deallocation.
//  AllocPC - The program counter at which the object was allocated.
//  FreePC  - The program counter at which the object was freed.
//  Canon   - The canonical address of the memory object.
//

static PDebugMetaData
createPtrMetaData (unsigned AllocID,
                   unsigned FreeID,
                   void * AllocPC,
                   void * FreePC,
                   void * Canon,
                   char * SourceFile,
                   unsigned lineno) {
  // FIXME:
  //  This will cause an allocation that is registered as an external
  //  allocation.  We need to use some internal allocation routine.
  //
  PDebugMetaData ret = (PDebugMetaData) malloc (sizeof(DebugMetaData));
  ret->allocID = AllocID;
  ret->freeID = FreeID;
  ret->allocPC = AllocPC;
  ret->freePC = FreePC;
  ret->canonAddr = Canon;
  ret->SourceFile = SourceFile;
  ret->lineno = lineno;

  return ret;
}

static inline void
updatePtrMetaData (PDebugMetaData debugmetadataptr,
                   unsigned globalfreeID,
                   void * paramFreePC) {
  debugmetadataptr->freeID = globalfreeID;
  debugmetadataptr->freePC = paramFreePC;
  return;
}


//
// Function: bus_error_handler()
//
// Description:
//  This is the signal handler that catches bad memory references.
//
static void
bus_error_handler (int sig, siginfo_t * info, void * context) {
  signal(SIGBUS, NULL);

  unsigned program_counter = 0;

  //
  // Get the address causing the fault.
  //
  void * faultAddr = info->si_addr, *end;
  PDebugMetaData debugmetadataptr;
  int fs = 0;

  //
  // Attempt to look up dangling pointer information for the faulting pointer.
  //
  fs = dummyPool.DPTree.find (info->si_addr, faultAddr, end, debugmetadataptr);

  //
  // If there is no dangling pointer information for the faulting pointer,
  // perhaps it is an Out of Bounds Rewrite Pointer.  Check for that now.
  //
  if (0 == fs) {
#if defined(__APPLE__)
#if defined(i386) || defined(__i386__) || defined(__x86__)
    // Cast parameters to the desired type
    ucontext_t * mycontext = (ucontext_t *) context;
    program_counter = mycontext->uc_mcontext->__ss.__eip;
#endif
#endif

#if SC_ENABLE_OOB
    void * start = faultAddr;
    void * tag = 0;
    void * end;

    if (OOBPool.OOB.find (faultAddr, start, end, tag)) {
      char * Filename = (char *)(RewriteSourcefile[faultAddr]);
      unsigned lineno = RewriteLineno[faultAddr];
      ReportOOBPointer (program_counter,
                        tag,
                        faultAddr,
                        RewrittenObjs[faultAddr].first,
                        RewrittenObjs[faultAddr].second,
                        Filename,
                        lineno);
      abort();
    }
#endif
    extern FILE * ReportLog;
    fprintf(ReportLog, "signal handler: no debug meta data for %p: eip=%p\n", faultAddr, (void*)program_counter);
    fflush(ReportLog);
    abort();
  }

 
  // FIXME: Correct the semantics for calculating NumPPage 
  unsigned NumPPage;
  unsigned offset = (unsigned) ((long)info->si_addr & (PPageSize - 1) );
  unsigned int len = (unsigned char *)(end) - (unsigned char *)(faultAddr) + 1;
  NumPPage = (len / PPageSize) + 1;
  if ( (len - (NumPPage-1) * PPageSize) > (PPageSize - offset) )
    NumPPage++;
 
  // This is necessary so that the program continues execution,
  //  especially in debugging mode 
  UnprotectShadowPage((void *)((long)info->si_addr & ~(PPageSize - 1)), NumPPage);
  
  //void* S = info->si_addr;
  // printing reports
  void * address = 0;
  program_counter = 0;
  unsigned alloc_pc = 0;
  unsigned free_pc = 0;
  unsigned allocID = 0;
  unsigned freeID = 0;

#if defined(__APPLE__)
#if defined(i386) || defined(__i386__) || defined(__x86__)
  // Cast parameters to the desired type
  ucontext_t * mycontext = (ucontext_t *) context;
  program_counter = mycontext->uc_mcontext->__ss.__eip;
#endif
  alloc_pc = ((unsigned) (debugmetadataptr->allocPC)) - 5;
  free_pc  = ((unsigned) (debugmetadataptr->freePC)) - 5;
  allocID  = debugmetadataptr->allocID;
  freeID   = debugmetadataptr->freeID;
#endif
  
  ReportDanglingPointer (address, program_counter,
                         alloc_pc, allocID,
                         free_pc, freeID);

  // reinstall the signal handler for subsequent faults
  struct sigaction sa;
  sa.sa_sigaction = bus_error_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGBUS, &sa, NULL) == -1)
    printf("sigaction installer failed!");
  if (sigaction(SIGSEGV, &sa, NULL) == -1)
    printf("sigaction installer failed!");
  
  return;
}

//
// Function: pool_protect_object()
//
// Description:
//  This function modifies the page protections of an object so that it is no
//  longer writeable.
//
// Inputs:
//  Node - A pointer to the beginning of the object that should be marked as
//         read-only.
// Notes:
//  This function should only be called when dangling pointer detection is
//  enabled.
//
void
pool_protect_object (void * Node) {
  // The start and end of the object as registered in the dangling pointer
  // object metapool
  void * start = 0, * end = 0;

  //
  // Retrieve the debug information about the node.  This will include a
  // pointer to the canonical page.
  //
  PDebugMetaData debugmetadataptr = 0;
  bool found = dummyPool.DPTree.find (Node, start, end, debugmetadataptr);

  // Assert that we either didn't find the object or we found the object *and*
  // it has meta-data associated with it.
  assert ((!found || (found && debugmetadataptr)) &&
          "poolfree: No debugmetadataptr\n");

  //
  // If the object is not found, return.
  //
  if (!found) return;

  //
  // Determine the number of pages that the object occupies.
  //
  unsigned len = (unsigned)end - (unsigned)start;
  unsigned offset = (unsigned)((long)Node & (PPageSize - 1));
  unsigned NumPPage = (len / PPageSize) + 1;
  if ( (len - (NumPPage-1) * PPageSize) > (PPageSize - offset) )
    NumPPage++;

  // Protect the shadow pages of the object
  ProtectShadowPage((void *)((long)Node & ~(PPageSize - 1)), NumPPage);
  return;
}

//
// Function: poolcalloc_debug()
//
// Description:
//  This is the same as pool_calloc but with source level debugging
//  information.
//
// Inputs:
//  Pool        - The pool from which to allocate the elements.
//  Number      - The number of elements to allocate.
//  NumBytes    - The size of each element in bytes.
//  SourceFilep - A pointer to the source filename in which the caller is
//                located.
//  lineno      - The line number at which the call occurs in the source code.
//
// Return value:
//  NULL - The allocation did not succeed.
//  Otherwise, a fresh pointer to the allocated memory is returned.
//
// Notes:
//  Note that this function calls poolregister() directly because the SAFECode
//  transforms do not add explicit calls to poolregister().
//
void *
__sc_dbg_src_poolcalloc (DebugPoolTy *Pool,
                         unsigned Number,
                         unsigned NumBytes,
                         const char * SourceFilep,
                         unsigned lineno) {
  void * New = __sc_dbg_src_poolalloc (Pool, Number * NumBytes, SourceFilep, lineno);
  if (New) {
    bzero (New, Number * NumBytes);
    __sc_dbg_src_poolregister (Pool, New, Number * NumBytes, SourceFilep, lineno);
  }
  if (logregs) {
    fprintf (ReportLog, "poolcalloc_debug: %p: %p %x: %s %d\n", (void*) Pool, (void*)New, Number * NumBytes, SourceFilep, lineno);
    fflush (ReportLog);
  }
  return New;
}

void *
__sc_dbg_poolcalloc (DebugPoolTy *Pool, unsigned Number, unsigned NumBytes) {
  return __sc_dbg_src_poolcalloc (Pool, Number, NumBytes, "<unknown>", 0);
}

void *
__sc_dbg_poolrealloc(DebugPoolTy *Pool, void *Node, unsigned NumBytes) {
  //
  // If the object has never been allocated before, allocate it now.
  //
  if (Node == 0) {
    void * New = __pa_bitmap_poolalloc(Pool, NumBytes);
    __sc_dbg_poolregister (Pool, New, NumBytes);
    return New;
  }

  //
  // Reallocate an object to 0 bytes means that we wish to free it.
  //
  if (NumBytes == 0) {
    pool_protect_object (Node);
    __sc_dbg_poolunregister(Pool, Node);
    __pa_bitmap_poolfree(Pool, Node);
    return 0;
  }

  //
  // Otherwise, we need to change the size of the allocated object.  For now,
  // we will simply allocate a new object and copy the data from the old object
  // into the new object.
  //
  void *New;
  if ((New = __pa_bitmap_poolalloc(Pool, NumBytes)) == 0)
    return 0;

  //
  // Get the bounds of the old object.  If we cannot get the bounds, then
  // simply fail the allocation.
  //
  void * S, * end;
  if ((!(Pool->Objects.find (Node, S, end))) || (S != Node)) {
    return 0;
  }

  //
  // Register the new object with the pool.
  //
  __sc_dbg_poolregister (Pool, New, NumBytes);

  //
  // Determine the number of bytes to copy into the new object.
  //
  unsigned length = NumBytes;
  if ((((unsigned)(end)) - ((unsigned)(S)) + 1) < NumBytes) {
    length = ((unsigned)(end)) - ((unsigned)(S)) + 1;
  }

  //
  // Copy the contents of the old object into the new object.
  //
  memcpy(New, Node, length);

  //
  // Invalidate the old object and its bounds and return the pointer to the
  // new object.
  //
  pool_protect_object (Node);
  __sc_dbg_poolunregister(Pool, Node);
  __pa_bitmap_poolfree(Pool, Node);
  return New;
}
