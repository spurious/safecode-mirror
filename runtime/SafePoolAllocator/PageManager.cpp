//===- PageManager.cpp - Implementation of the page allocator -------------===//
// 
//                       The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the PageManager.h interface.
//
//===----------------------------------------------------------------------===//

#include "PageManager.h"
#ifndef _POSIX_MAPPED_FILES
#define _POSIX_MAPPED_FILES
#endif
#include <unistd.h>
#include "poolalloc/Support/MallocAllocator.h"
#include "poolalloc/MMAPSupport.h"
#include <iostream>
#include <vector>
#include <cassert>

// this is for dangling pointer detection in Mac OS X
#if defined(__APPLE__)
#include <mach/mach_vm.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#endif

// Define this if we want to use memalign instead of mmap to get pages.
// Empirically, this slows down the pool allocator a LOT.
#define USE_MEMALIGN 0
extern "C" {
unsigned PageSize = 0;
}
extern unsigned poolmemusage;
void InitializePageManager() {
  if (!PageSize) {
    PageSize =  16 * sysconf(_SC_PAGESIZE) ;
  }
}

unsigned PPageSize = sysconf(_SC_PAGESIZE);
static unsigned logregs = 0;

#if !USE_MEMALIGN
static void *GetPages(unsigned NumPages) {
#if defined(i386) || defined(__i386__) || defined(__x86__)
  /* Linux and *BSD tend to have these flags named differently. */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
# define MAP_ANONYMOUS MAP_ANON
#endif /* defined(MAP_ANON) && !defined(MAP_ANONYMOUS) */
#elif defined(sparc) || defined(__sparc__) || defined(__sparcv9)
  /* nothing */
#elif defined(__APPLE__)
  /* On MacOS X, just use valloc */
#else
  std::cerr << "This architecture is not supported by the pool allocator!\n";
  abort();
#endif

#if defined(__linux__)
#define fd 0
#else
#define fd -1
#endif
  void *Addr;
  //MMAP DOESNOT WORK !!!!!!!!
  //  Addr = mmap(0, NumPages*PageSize, PROT_READ|PROT_WRITE,
  //                 MAP_SHARED|MAP_ANONYMOUS, fd, 0);
  //  void *pa = malloc(NumPages * PageSize);
  //  assert(Addr != MAP_FAILED && "MMAP FAILED!");
#if defined(__linux__)
  Addr = mmap(0, NumPages * PageSize, PROT_READ|PROT_WRITE,
                                      MAP_SHARED |MAP_ANONYMOUS, -1, 0);
  if (Addr == MAP_FAILED) {
     perror ("mmap:");
     fflush (stdout);
     fflush (stderr);
     assert(0 && "valloc failed\n");
  }
#else
#if POSIX_MEMALIGN
   if (posix_memalign(&Addr, PageSize, NumPages*PageSize) != 0){
     assert(0 && "memalign failed \n");
   }
#else
   if ((Addr = valloc (NumPages*PageSize)) == 0){
     perror ("valloc:");
     fflush (stdout);
     fflush (stderr);
     assert(0 && "valloc failed \n");
   } else {
#if 0
    fprintf (stderr, "valloc: Allocated %x\n", NumPages);
    fflush (stderr);
#endif
   }
#endif
#endif
  poolmemusage += NumPages * PageSize;
  memset(Addr, initvalue, NumPages *PageSize);
  return Addr;
}
#endif

// Explicitly use the malloc allocator here, to avoid depending on the C++
// runtime library.
typedef std::vector<void*, llvm::MallocAllocator<void*> > FreePagesListType;

static FreePagesListType &getFreePageList() {
  static FreePagesListType FreePages;

  return FreePages;
}

//
// Function: RemapPage()
//
// Description:
//  Create another mapping of the memory object so that it appears in multiple
//  locations of the virtual address space.
//
// Inputs:
//  va - Virtual address of the memory object to remap.  It does not need to be
//       page aligned.
//
//  NumByte - The length of the memory object in bytes.
//
// Notes:
//  This function must generally determine the set of pages occupied by the
//  memory object and remap those pages.  This is because most operating
//  systems can only remap memory at page granularity.
//
#if defined(__APPLE__)
void *
RemapPage (void * va, unsigned NumByte) {
  kern_return_t      kr;
  mach_vm_address_t  target_addr = 0;
  mach_vm_address_t  source_addr;
  vm_prot_t          prot_cur = VM_PROT_READ | VM_PROT_WRITE;
  vm_prot_t          prot_max = VM_PROT_READ | VM_PROT_WRITE;

  source_addr = (mach_vm_address_t) ((unsigned long)va & ~(PPageSize - 1));
  unsigned offset = (unsigned long)va & (PPageSize - 1);
  unsigned NumPPage = 0;

  NumPPage = (NumByte / PPageSize) + 1;

  //if((unsigned)va > 0x2f000000) {
  //  logregs = 1;
  //}

  if (logregs) {
    fprintf (stderr, " RemapPage:117: source_addr = 0x%016p, offset = 0x%016x, NumPPage = %d\n", (void*)source_addr, offset, NumPPage);
    fflush(stderr);
  }

#if 0
  // FIX ME!! when there's time, check out why this doesn't work
  if ( (NumByte - (NumPPage-1) * PPageSize) > (PPageSize - offset) ) {
    NumPPage++;
    NumByte = NumPPage * PPageSize;
  }
#endif

  unsigned byteToMap = NumByte + offset;

  if (logregs) {
    fprintf(stderr, " RemapPage127: remapping page of size %d covering %d page with offset %d and byteToMap = %d",
    NumByte, NumPPage, offset, byteToMap);
    fflush(stderr);
  }
  kr = mach_vm_remap (mach_task_self(),
                      &target_addr,
                      byteToMap,
                      0,
                      TRUE,
                      mach_task_self(),
                      source_addr,
                      FALSE,
                      &prot_cur,
                      &prot_max,
                      VM_INHERIT_SHARE); 
 
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, " mach_vm_remap error: %d \n", kr);
    fprintf(stderr, " failed to remap %dB of memory from source_addr = 0x%08x\n", byteToMap, (unsigned)source_addr);
    //printf(" no of pages used %d %d  %d\n", AddressSpaceUsage1, AddressSpaceUsage2, AddressSpaceUsage2+AddressSpaceUsage1);
    fprintf(stderr, "%s\n", mach_error_string(kr));
    mach_error("mach_vm_remap:",kr); // just to make sure I've got this error right
    fflush(stderr);
    //goto repeat;
    //abort();
  }

  if (logregs) {
    fprintf(stderr, " RemapPage:160: remap succeeded to addr 0x%08x\n", (unsigned)target_addr);
    fflush(stderr);
  }
  va = (void *) target_addr;
  return va;
 
/* 
#ifdef STATISTIC
   AddressSpaceUsage2++;
#endif
*/
}
#else
#include <sys/syscall.h>

extern "C"
int
llva_syscall6 (int sysnum, int arg1, int arg2, int arg3, int arg4, int arg5,
                           int arg6)
{
  int ret_value;

  /*
   * Perform the system call.  Allow GCC to assign the variables into their
   * required registers.
   */
  __asm__ __volatile__ ("int $0x80\n"
                        : "=a" (ret_value)
                        :  "a" (sysnum), "b" (arg1), "c" (arg2), "d" (arg3),
                           "S" (arg4), "D" (arg5));

  return ret_value;
}

void *
RemapPage (void * va, unsigned NumByte) {
  void *  target_addr = 0;
  void *  source_addr;
  void *  finish_addr;

  //
  // Find the beginning and end of the physical pages for this memory object.
  //
  source_addr = (void *) ((unsigned long)va & ~(PageSize - 1));
  finish_addr = (void *) (((unsigned long)va + NumByte) & ~(PPageSize - 1));

  unsigned int NumPages = ((unsigned)finish_addr - (unsigned)source_addr) / PPageSize;
  if (!NumPages) NumPages = 1;

  //
  // Find the length in bytes of the memory we want to remap.
  //
  unsigned length = PageSize;

fprintf (stderr, "remap: %x %x -> %x %x\n", va, NumByte, source_addr, length);
fflush (stderr);
#if 0
  target_addr = mremap (source_addr, 0, PageSize, MREMAP_MAYMOVE, 0);
#else
    int flags = MREMAP_MAYMOVE;
    target_addr = (void *)llva_syscall6 (SYS_mremap, (int)source_addr, 0, PageSize,
                             (int)(flags), 0, 0);
#endif
  if (target_addr == MAP_FAILED) {
    perror ("RemapPage: Failed to create shadow page: ");
  }

#if 1
  volatile unsigned int * p = (unsigned int *) source_addr;
  volatile unsigned int * q = (unsigned int *) target_addr;

  p[0] = 0xbeefbeef;
fprintf (stderr, "value: %x=%x, %x=%x\n", p, p[0], q, q[0]);
  p[0] = 0xdeeddeed;
fprintf (stderr, "value: %x=%x, %x=%x\n", p, p[0], q, q[0]);
fflush (stderr);
#endif
  return target_addr;
}
#endif

/// AllocatePage - This function returns a chunk of memory with size and
/// alignment specified by PageSize.
void *AllocatePage() {

  FreePagesListType &FPL = getFreePageList();

  if (!FPL.empty()) {
    void *Result = FPL.back();
      FPL.pop_back();
      return Result;
  }

  // Allocate several pages, and put the extras on the freelist...
  unsigned NumToAllocate = 8;
  char *Ptr = (char*)GetPages(NumToAllocate);

  for (unsigned i = 1; i != NumToAllocate; ++i)
    FPL.push_back(Ptr+i*PageSize);
  return Ptr;
}

void *AllocateNPages(unsigned Num) {
  if (Num <= 1) return AllocatePage();
  return GetPages(Num);
}


// MprotectPage - This function changes the protection status of the page to become
//                 none-accessible
#if defined(__APPLE__)
void
MprotectPage (void *pa, unsigned numPages) {
  kern_return_t kr;
  kr = mprotect(pa, numPages * PageSize, PROT_NONE);
  if (kr != KERN_SUCCESS)
    perror(" mprotect error: Failed to mark page non-accessible\n");
  return;
}
#else
void
MprotectPage (void *pa, unsigned numPages) {
  int kr;
  kr = mprotect(pa, numPages * PageSize, PROT_NONE);
  if (kr != -1)
    perror(" mprotect error: Failed to mark page non-accessible\n");
  return;
}
#endif

/// FreePage - This function returns the specified page to the pagemanager for
/// future allocation.
#define THRESHOLD 5
void FreePage(void *Page) {
  FreePagesListType &FPL = getFreePageList();
  FPL.push_back(Page);
  munmap(Page, 1);
  /*
  if (FPL.size() >  THRESHOLD) {
    //    printf( "pool allocator : reached a threshold \n");
    //    exit(-1); 
    munmap(Page, PageSize);
    poolmemusage -= PageSize;
  }
  */
}

// ProtectShadowPage - Protects shadow page that begins at beginAddr, spanning
//                     over PageNum
#if defined(__APPLE__)
void
ProtectShadowPage (void * beginPage, unsigned NumPPages)
{
  kern_return_t kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_NONE);
  if (kr != KERN_SUCCESS)
    perror(" mprotect error: Failed to protect shadow page\n");
  return;
}
#else
void
ProtectShadowPage (void * beginPage, unsigned NumPPages)
{
  int kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_NONE);
  if (kr == -1)
    perror(" mprotect error: Failed to protect shadow page\n");
  return;
}
#endif

// UnprotectShadowPage - Unprotects the shadow page in the event of fault when
//                       accessing protected shadow page in order to
//                       resume execution
#if defined(__APPLE__)
void
UnprotectShadowPage (void * beginPage, unsigned NumPPages)
{
  kern_return_t kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_READ | PROT_WRITE);
  if (kr != KERN_SUCCESS)
    perror(" unprotect error: Failed to make shadow page accessible \n");
  return;
}
#else
void
UnprotectShadowPage (void * beginPage, unsigned NumPPages)
{
  int kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_READ | PROT_WRITE);
  if (kr == -1)
    perror(" unprotect error: Failed to make shadow page accessible \n");
  return;
}
#endif

