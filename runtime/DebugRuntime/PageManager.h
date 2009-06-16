//===- PageManager.h - Allocates memory on page boundaries ------*- C++ -*-===//
// 
//                          The SAFECode Compiler 
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

#ifndef _SC_DEBUG_PAGEMANAGER_H_
#define _SC_DEBUG_PAGEMANAGER_H_

#include "safecode/Runtime/PageManager.h"

NAMESPACE_SC_BEGIN

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
//  o) Other platforms - We allocate a range of memory and disable read and
//                       write permissions for the pages contained within it.
//
#if defined(__linux__)
static const unsigned InvalidUpper = 0xf0000000;
static const unsigned InvalidLower = 0xc0000000;
#endif

/// Special implemetation for dangling pointer detection

// RemapObject - This function is used by the dangling pool allocator in order
//               to remap canonical pages to shadow pages.
void * RemapObject(void* va, unsigned NumByte);

// MProtectPage - Protects Page passed in by argument, raising an exception
//                or traps at future access to Page
void MProtectPage(void * Page, unsigned NumPages);

// ProtectShadowPage - Protects shadow page that begins at beginAddr, spanning
//                     over NumPages
void ProtectShadowPage(void * beginPage, unsigned NumPPages);

// UnprotectShadowPage - Unprotects the shadow page in the event of fault when
//                       accessing protected shadow page in order to
//                       resume execution
void UnprotectShadowPage(void * beginPage, unsigned NumPPage);

NAMESPACE_SC_END
#endif
