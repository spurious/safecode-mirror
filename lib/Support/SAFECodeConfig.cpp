//===- SAFECodeConfig.cpp ---------------------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Parse and record all configuration parameters required by SAFECode.
//
//===----------------------------------------------------------------------===//

#include "safecode/SAFECodeConfig.h"

#include "llvm/Support/CommandLine.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

namespace {

  cl::opt<bool>
  DanglingPointerChecks("dpchecks", cl::init(false), cl::desc("Perform Dangling Pointer Checks"));

#ifdef SC_ENABLE_OOB
  cl::opt<bool>
  RewritePtrs("rewrite-oob", cl::init(false), cl::desc("Rewrite Out of Bound (OOB) Pointers"));
#else
  bool RewritePtrs = false;
#endif
  
  cl::opt<bool>
  StopOnFirstError("terminate", cl::init(false),
                   cl::desc("Terminate when an Error Ocurs"));
  
  cl::opt<bool> NoStaticChecks ("disable-staticchecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable Static Array Bounds Checks"));

  cl::opt<bool>
  EnableSVA("sva", cl::init(false), 
            cl::desc("Enable SVA-Kernel specific operations"));
}

namespace {
  SAFECodeConfiguration::StaticCheckTy
    none  = SAFECodeConfiguration::ABC_CHECK_NONE,
    local = SAFECodeConfiguration::ABC_CHECK_LOCAL, 
    full  = SAFECodeConfiguration::ABC_CHECK_FULL;
  
  cl::opt<SAFECodeConfiguration::StaticCheckTy>
  StaticChecks("static-abc", cl::init(SAFECodeConfiguration::ABC_CHECK_NONE),
               cl::desc("Static array bounds check analysis"),
               cl::values
               (clEnumVal(none,
                          "No static array bound checks"),
                clEnumVal(local,
                          "Local static array bound checks"),
                clEnumVal(full,
                          "Omega static array bound checks"),
                clEnumValEnd));
}

SAFECodeConfiguration * SCConfig;

SAFECodeConfiguration *
SAFECodeConfiguration::create() {
  return new SAFECodeConfiguration();
}

SAFECodeConfiguration::SAFECodeConfiguration() {
  // TODO: Move all cl::opt to this file and parse them here
  this->DanglingPointerChecks = DanglingPointerChecks;
  this->RewriteOOB = RewritePtrs;
  this->TerminateOnErrors = StopOnFirstError;
  this->StaticCheckType = StaticChecks;
  // TODO: DSAType
  this->SVAEnabled = EnableSVA;
}

NAMESPACE_SC_END
