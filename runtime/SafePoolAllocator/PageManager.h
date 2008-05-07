//===- PageManager.h - Allocates memory on page boundaries ------*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines the interface used by the pool allocator to allocate memory
// on large alignment boundaries.
//
//===----------------------------------------------------------------------===//

#ifndef PAGEMANAGER_H
#define PAGEMANAGER_H

/// InitializePageManager - This function must be called before any other page
/// manager accesses are performed.  It may be called multiple times.
/// 
void InitializePageManager();

/// PageSize - Contains the size of the unit of memory allocated by
/// AllocatePage.  This is a value that is typically several kilobytes in size,
/// and is guaranteed to be a power of two.
///
extern "C" unsigned PageSize;

extern unsigned PPageSize;

/// AllocatePage - This function returns a chunk of memory with size and
/// alignment specified by getPageSize().
void *AllocatePage();

// RemapPage - This function is used by the dangling pool allocator in order
//				to remap canonical page to shadow page.
void * RemapPage(void* va, unsigned NumByte);

/// AllocateNPages - Allocates Num number of pages
void *AllocateNPages(unsigned Num);

// MProtectPage - Protects Page passed in by argument, raising an exception
//					or traps at future access to Page
void MProtectPage(void * Page, unsigned NumPages);

// ProtectShadowPage - Protects shadow page that begins at beginAddr, spanning
//						over NumPages
void ProtectShadowPage(void * beginPage, unsigned NumPPages);

// UnprotectShadowPage - Unprotects the shadow page in the event of fault when
//							accessing protected shadow page in order to
//							resume execution
void UnprotectShadowPage(void * beginPage, unsigned NumPPage);

/// FreePage - This function returns the specified page to the pagemanager for
/// future allocation.
void FreePage(void *Page);

//
// The lower and upper bound of an unmapped memory region.  This range is used
// for rewriting pointers that go one beyond the edge of an object so that they
// can be used for comparisons but will generate a fault if used for loads or
// stores.
//
// There are a few restrictions:
//  1) I *think* InvalidUpper must be on a page boundary.
//  2) None of the values can be reserved pointer values.  Such values include:
//      0 - This is the NULL pointer.
//      1 - This is a reserved pointer in the Linux kernel.
//      2 - This is another reserved pointer in the Linux kernel.
//
// Here's the breakdown of how it works on various operating systems:
//  o) Linux           - We use the kernel's reserved address space (which is
//                       inaccessible from applications).
//  o) Other platforms - We use the first page of memory which is typically
//                       unmapped.
//
#if defined(__linux__)
static const unsigned InvalidUpper = 0xf0000000;
static const unsigned InvalidLower = 0xc0000000;
#else
static const unsigned InvalidUpper = 0x00001000;
static const unsigned InvalidLower = 0x00000003;
#endif

//
// Value used to initialize memory.
//
static const unsigned initvalue = 0xcc;

#endif
