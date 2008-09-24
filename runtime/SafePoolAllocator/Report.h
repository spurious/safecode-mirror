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

#include <stdio.h>

FILE * ReportLog;
static unsigned alertNum = 0;

//
// Function: printAlertHeader()
//
// Description:
//  Increment the alert number and print a header for this report message.
//
static unsigned
printAlertHeader (void) {
  fprintf (ReportLog, "=======+++++++    SAFECODE RUNTIME ALERT #%04d   +++++++=======\n",
          ++alertNum);
  return alertNum;
}

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
static void
ReportDanglingPointer (void * addr,
                       unsigned pc,
                       unsigned allocpc,
                       unsigned allocgen,
                       unsigned freepc,
                       unsigned freegen) {
  // Print the header and get the ID for this report
  unsigned id = printAlertHeader();

  fprintf (ReportLog, "%04d: Dangling pointer access to memory address 0x%08x \n",
          id,
          (unsigned)addr);
  fprintf (ReportLog, "%04d:                        at program counter 0x%08x\n", id, (unsigned)pc);
  fprintf (ReportLog, "%04d:\tObject allocated at program counter   : 0x%08x \n", id, (unsigned)allocpc);
  fprintf (ReportLog, "%04d:\tObject allocation generation number   : %d \n", id, allocgen);
  fprintf (ReportLog, "%04d:\tObject freed at program counter       : 0x%08x \n", id, freepc);
  fprintf (ReportLog, "%04d:\tObject free generation number         : %d \n", id, freegen);
  fprintf(ReportLog, "=======+++++++    end of runtime error report    +++++++=======\n");
  fflush (ReportLog);
  return;
}

//
// Function: ReportBoundsCheck()
//
// Description:
//  Generate a report for a bounds check violation.
//
// Inputs:
//  src      - The source pointer for the failed indexing operation.
//  dest     - The result pointer for the failed indexing operation.
//  pc       - The program counter of the failed run-time check.
//  objstart - The start of the object in which the source pointer was found.
//  objlen   - The length of the object in which the source pointer was found.
//
// Note:
//  An objstart and objlen of 0 indicate that the source pointer was not found
//  within a valid object.
//
static void
ReportBoundsCheck (unsigned src,
                   unsigned dest,
                   unsigned pc,
                   unsigned objstart,
                   unsigned objlen) {
  // Print the header and get the ID for this report
  unsigned id = printAlertHeader();

  fprintf (ReportLog, "%04d: Bounds violation to memory address 0x%08x\n", id, dest);
  fprintf (ReportLog, "%04d:                 at program counter 0x%08x\n", id, pc);
  fprintf (ReportLog, "%04d:\tIndex source pointer : 0x%08x \n", id, src);
  fprintf (ReportLog, "%04d:\tIndex result pointer : 0x%08x \n", id, dest);
  if (objstart || objlen) {
    fprintf (ReportLog, "%04d:\tObject lower bound   : 0x%08x \n", id, objstart);
    fprintf (ReportLog, "%04d:\tObject upper bound   : 0x%08x \n", id, objstart+objlen);
    fprintf(ReportLog, "=======+++++++    end of runtime error report    +++++++=======\n");
  } else {
    fprintf (ReportLog, "%04d:\tNot found within object\n", id);
  }
  fflush (ReportLog);
  return;
}

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
static void
ReportExactCheck (unsigned src,
                  unsigned dest,
                  unsigned pc,
                  unsigned objstart,
                  unsigned objlen) {
  // Print the header and get the ID for this report
  unsigned id = printAlertHeader();

  fprintf (ReportLog, "%04d: Bounds violation to memory address 0x%08x (ExactCheck)\n", id, dest);
  fprintf (ReportLog, "%04d:                 at program counter 0x%08x\n", id, pc);
  fprintf (ReportLog, "%04d:\tIndex result pointer : 0x%08x \n", id, dest);
  if (objstart || objlen) {
    fprintf (ReportLog, "%04d:\tObject lower bound   : 0x%08x \n", id, objstart);
    fprintf (ReportLog, "%04d:\tObject upper bound   : 0x%08x \n", id, objstart+objlen);
    fprintf(ReportLog, "=======+++++++    end of runtime error report    +++++++=======\n");
  } else {
    fprintf (ReportLog, "%04d:\tNot found within object\n", id);
  }
  fflush (ReportLog);
  return;
}

