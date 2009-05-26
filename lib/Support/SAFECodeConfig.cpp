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

static cl::opt<bool>
DanglingPointerChecks("dpchecks", cl::init(false), cl::desc("Perform Dangling Pointer Checks"));

#ifdef SC_ENABLE_OOB
static cl::opt<bool>
RewritePtrs("rewrite-oob", cl::init(false), cl::desc("Rewrite Out of Bound (OOB) Pointers"));
#else
static bool RewritePtrs = false;
#endif

static cl::opt<bool>
StopOnFirstError("terminate", cl::init(false),
                              cl::desc("Terminate when an Error Ocurs"));

static cl::opt<bool> NoStaticChecks ("disable-staticchecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable Static Array Bounds Checks"));

static cl::opt<bool>
EnableSVA("sva", cl::init(false), 
          cl::desc("Enable SVA-Kernel specific operations"));

NAMESPACE_SC_BEGIN

SAFECodeConfiguration * SCConfig;

SAFECodeConfiguration *
SAFECodeConfiguration::create() {
  return new SAFECodeConfiguration();
}

SAFECodeConfiguration::SAFECodeConfiguration() {
  // TODO: Move all cl::opt to this file and parse them here
  this->DanglingPointerChecks = ::DanglingPointerChecks;
  this->RewriteOOB = ::RewritePtrs;
  this->TerminateOnErrors = ::StopOnFirstError;
  this->StaticCheckType = ::NoStaticChecks ? ABC_CHECK_NONE : ABC_CHECK_FULL;
  // TODO: DSAType
  this->SVAEnabled = ::EnableSVA;
}

NAMESPACE_SC_END
