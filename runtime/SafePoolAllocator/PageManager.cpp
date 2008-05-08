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
#include <mach/mach_vm.h>
#include <mach/mach.h>
#include <mach/mach_error.h>

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
void MprotectPage(void *pa, unsigned numPages) {
  kern_return_t kr;
  kr = mprotect(pa, numPages * PageSize, PROT_NONE);
  if (kr != KERN_SUCCESS)
    perror(" mprotect error \n");
  return;
}



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
void ProtectShadowPage(void * beginPage, unsigned NumPPages)
{
  kern_return_t kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_NONE);
  if (kr != KERN_SUCCESS)
    perror(" mprotect error \n");
  return;
}

// UnprotectShadowPage - Unprotects the shadow page in the event of fault when
//                       accessing protected shadow page in order to
//                       resume execution
void UnprotectShadowPage(void * beginPage, unsigned NumPPages)
{
  kern_return_t kr;
  kr = mprotect(beginPage, NumPPages * 4096, PROT_READ | PROT_WRITE);
  if (kr != KERN_SUCCESS)
    perror(" unprotect error \n");
  return;
}

