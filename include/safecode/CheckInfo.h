//===- CheckInfo.h - Information about SAFECode run-time checks --*- C++ -*---//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements structures containing data about the various run-time
// checks that SAFECode inserts into code.
//
//===----------------------------------------------------------------------===//

#ifndef _SC_CHECKINFO_H_
#define _SC_CHECKINFO_H_

#include "llvm/Function.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Instructions.h"

using namespace llvm;

namespace {

enum CheckType {memcheck, gepcheck, funccheck};

//
// Structure: CheckInfo
//
// Description:
//  This structure describes a run-time check.
//
struct CheckInfo {
  // The name of the function implementing the run-time check
  const char * name;

  // The name of the complete version of the check
  const char * completeName;

  // The argument of the checked pointer.
  unsigned char argno;

  // A boolean indicating whether it is a memory check or a bounds check
  CheckType checkType;

  // The argument of the length (if appropriate)
  unsigned char lenArg;

  // A boolean indicating whether the check is complete
  bool isComplete;

  bool isMemCheck (void) const {
    return checkType == memcheck;
  }

  Value * getCheckedPointer (CallInst * CI) const {
    CallSite CS(CI);
    return CS.getArgument (argno);
  }

  Value * getCheckedLength (CallInst * CI) const {
    if (lenArg) {
      CallSite CS(CI);
      return CS.getArgument (lenArg);
    }

    return 0;
  }
};

//
// Create a table describing all of the SAFECode run-time checks.
//
static const unsigned numChecks = 20;
static const struct CheckInfo RuntimeChecks[numChecks] = {
  // Regular checking functions
  {"poolcheck",        "lscheck",      1, memcheck,  2, true},
  {"poolcheckui",      "lscheck",      1, memcheck,  2, false},
  {"poolcheckalign",   "lscheckalign", 1, memcheck,  0, true},
  {"poolcheckalignui", "lscheckalign", 1, memcheck,  0, false},
  {"boundscheck",      "boundscheck",  2, gepcheck, 0, true},
  {"boundscheckui",    "boundscheck",  2, gepcheck, 0, false},
  {"exactcheck2",      "exactcheck2",  1, gepcheck, 0, true},
  {"fastlscheck",      "fastlscheck",  1, memcheck,  3, true},
  {"funccheck",        "funccheck",    0, funccheck,  0, true},
  {"funccheckui",      "funccheck",    0, funccheck,  0, false},

  // Debug versions of the above
  {"poolcheck_debug",        "poolcheck_debug",      1, memcheck,  2, true},
  {"poolcheckui_debug",      "poolcheck_debug",      1, memcheck,  2, false},
  {"poolcheckalign_debug",   "poolcheckalign_debug", 1, memcheck,  0, true},
  {"poolcheckalignui_debug", "poolcheckalign_debug", 1, memcheck,  0, false},
  {"boundscheck_debug",      "boundscheck_debug",  2, gepcheck, 0, true},
  {"boundscheckui_debug",    "boundscheck_debug",  2, gepcheck, 0, false},
  {"exactcheck2_debug",      "exactcheck2_debug",  1, gepcheck, 0, true},
  {"fastlscheck_debug",      "fastlscheck_debug",  1, memcheck, 3, true},
  {"funccheck_debug",        "funccheck_debug",    1, funccheck,  0, true},
  {"funccheckui_debug",      "funccheck_debug",    1, funccheck,  0, false}
};

//
// Function: isRuntimeCheck()
//
// Description:
//  Determine whether the function is one of the run-time checking functions.
//
// Return value:
//  true  - The function is a run-time check.
//  false - The function is not a run-time check.
//
static inline bool
isRuntimeCheck (const Function * F) {
  if (F->hasName()) {
    for (unsigned index = 0; index < numChecks; ++index) {
      if (F->getName() == RuntimeChecks[index].name) {
        return true;
      }
    }
  }

  return false;
}

//
// Function: findRuntimeCheck()
//
// Description:
//  Determine if this function is one of the run-time checking functions.  If
//  so, return the information about the run-time check.
//
// Inputs:
//  F - The function to check.
//
// Return value:
//  NULL - This is not a call to a run-time check.
//  Otherwise, a pointer to the proper run-time check entry is returned.
//
static inline const struct CheckInfo *
findRuntimeCheck (const Function * F) {
  if (F->hasName()) {
    for (unsigned index = 0; index < numChecks; ++index) {
      if (F->getName() == RuntimeChecks[index].name) {
        return &(RuntimeChecks[index]);
      }
    }
  }

  return 0;
}

}
#endif
