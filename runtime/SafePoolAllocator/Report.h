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

static unsigned alertNum = 0;

//
// Function: printAlertHeader()
//
// Description:
//  Increment the alert number and print a header for this report message.
//
static unsigned
printAlertHeader (void) {
  printf ("=======+++++++    SAFECODE RUNTIME ALERT #%04d   +++++++=======\n",
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

  printf ("%04d: Dangling pointer access to memory address 0x%08x \n",
          id,
          (unsigned)addr);
  printf ("%04d:                        at program counter 0x%08x\n", id, (unsigned)pc);
  printf ("%04d:\tObject allocated at program counter   : 0x%08x \n", id, (unsigned)allocpc);
  printf ("%04d:\tObject allocation generation number   : %d \n", id, allocgen);
  printf ("%04d:\tObject freed at program counter       : 0x%08x \n", id, freepc);
  printf ("%04d:\tObject free generation number         : %d \n", id, freegen);
  printf("=======+++++++    end of runtime error report    +++++++=======\n");
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

  printf ("%04d: Bounds violation to memory address 0x%08x\n", id, dest);
  printf ("%04d:                 at program counter 0x%08x\n", id, pc);
  printf ("%04d:\tIndex source pointer : 0x%08x \n", id, src);
  printf ("%04d:\tIndex result pointer : 0x%08x \n", id, dest);
  if (objstart || objlen) {
    printf ("%04d:\tObject lower bound   : 0x%08x \n", id, objstart);
    printf ("%04d:\tObject upper bound   : 0x%08x \n", id, objstart+objlen);
    printf("=======+++++++    end of runtime error report    +++++++=======\n");
  } else {
    printf ("%04d:\tNot found within object\n", id);
  }
  return;
}

