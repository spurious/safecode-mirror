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

#include "llvm/Support/CallSite.h"
#include "llvm/Instructions.h"

using namespace llvm;

namespace {

//
// Structure: CheckInfo
//
// Description:
//  This structure describes a run-time check.
//
struct CheckInfo {
  // The name of the function implementing the run-time check
  const char * name;

  // The argument of the checked pointer.
  unsigned char argno;

  // A boolean indicating whether it is a memory check or a bounds check
  bool isMemcheck;

  Value * getCheckedPointer (CallInst * CI) const {
    CallSite CS(CI);
    return CS.getArgument (argno);
  }
};
}

//
// Create a table describing all of the SAFECode run-time checks.
//
static const unsigned numChecks = 8;
static const struct CheckInfo RuntimeChecks[numChecks] = {
  {"lscheck",        1, true},
  {"lscheckui",      1, true},
  {"lscheckalign",   1, true},
  {"lscheckuialign", 1, true},
  {"boundscheck",    2, false},
  {"boundscheckui",  2, false},
  {"exactcheck2",    1, false},
  {"funccheck",      1, true}
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
#endif
