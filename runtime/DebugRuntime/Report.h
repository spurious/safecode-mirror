//===- Report.h - Debugging reports for bugs found by SAFECode ------------===//
// 
//                       The SAFECode Compiler Project
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements functions for creating reports for the SAFECode
// run-time.
//
//===----------------------------------------------------------------------===//

#ifndef _REPORT_H_
#define _REPORT_H_

#include "safecode/SAFECode.h"

#include <cstdio>

extern FILE * ReportLog;

//
// Function: printAlertHeader()
//
// Description:
//  Increment the alert number and print a header for this report message.
//
unsigned
printAlertHeader (void);

//
// Function: ReportDanglingPointer()
//
// Description:
//  Create a report entry for a dangling pointer error.
//
// Inputs:
//  addr     - The dangling pointer value that was dereferenced.
//  pc       - The program counter of the instruction.
//  allocpc  - The program counter at which the object was last allocated.
//  allocgen - The generation number of the allocation.
//  freepc   - The program counter at which the object was last freed.
//  freegen  - The generation number of the free.
//
void
ReportDanglingPointer (void * addr,
                       unsigned pc,
                       unsigned allocpc,
                       unsigned allocgen,
                       unsigned freepc,
                       unsigned freegen);

//
// Function: ReportLoadStoreCheck()
//
// Description:
//  Report a failure on a load or store check.
//
// Inputs:
//  ptr      - The pointer for the failed load/store operation.
//  pc       - The program counter of the failed run-time check.
//
void
ReportLoadStoreCheck (void * ptr,
                      void * pc,
                      const char * SourceFile,
                      unsigned lineno);
//
// Function: ReportBoundsCheck()
//
// Description:
//  Generate a report for a bounds check violation.
//
// Inputs:
//  src        - The source pointer for the failed indexing operation.
//  dest       - The result pointer for the failed indexing operation.
//  allocID    - The generation number of the object's allocation.
//  allocPC    - The program counter at which the object was last allocated.
//  pc         - The program counter of the failed run-time check.
//  objstart   - The start of the object in which the source pointer was found.
//  objlen     - The length of the object in which the source pointer was found.
//  SourceFile - The source file in which the check failed.
//  lineno     - The line number at which the check failed.
//  allocSF    - The source file in which the object was allocated.
//  allocLN    - The line number at which the object was allocated.
//
// Note:
//  An objstart and objlen of 0 indicate that the source pointer was not found
//  within a valid object.
//
void
ReportBoundsCheck (unsigned src,
                   unsigned dest,
                   unsigned allocID,
                   unsigned allocPC,
                   unsigned pc,
                   unsigned objstart,
                   unsigned objlen,
                   unsigned char * SourceFile,
                   unsigned lineno,
                   unsigned char * allocSF,
                   unsigned allocLN);

//
// Function: ReportExactCheck()
//
// Description:
//  Identical to ReportBoundsCheck() but does not use the start pointer.
//
// Inputs:
//  src      - The source pointer for the failed indexing operation (unused).
//  dest     - The result pointer for the failed indexing operation.
//  pc       - The program counter of the failed run-time check.
//  objstart - The start of the object in which the source pointer was found.
//  objlen   - The length of the object in which the source pointer was found.
//
// Note:
//  An objstart and objlen of 0 indicate that the source pointer was not found
//  within a valid object.
//
void
ReportExactCheck (unsigned src,
                  unsigned dest,
                  unsigned pc,
                  unsigned objstart,
                  unsigned objlen,
                  const char * SourceFile,
                  unsigned lineno);

//
// Function: ReportOOBPointer()
//
// Description:
//  Generate a report for the use of an out of bounds (OOB) pointer.
//
// Inputs:
//  pc         - The program counter of the failed run-time check.
//  ptr        - The out of bounds pointer.
//  oobp       - The rewritten pointer that caused the fault.
//  ObjStart   - The start of the object from which the pointer came.
//  Objend     - The end of the object from which the pointer came.
//  SourceFile - The source file in which the pointer went out of bounds.
//  lineno     - The line number at which the pointer went out of bounds.
//
void
ReportOOBPointer (unsigned pc,
                  const void * ptr,
                  const void * oobp,
                  const void * ObjStart,
                  const void * ObjEnd,
                  const char * SourceFile,
                  unsigned lineno);

//
// Function: ReportInvalidFree()
//
// Description:
//  Generate a report for an invalid free.
//
// Inputs:
//  pc         - The program counter at which the invalid free occurred.
//  ptr        - The invalid pointer passed to poolfree().
//  SourceFile - The source file in which the call to free() occurred.
//  lineno     - The line number at which the call to free() occurred.
//
void
ReportInvalidFree (unsigned pc,
                   void * ptr,
                   const char * SourceFile,
                   unsigned lineno);

#endif
