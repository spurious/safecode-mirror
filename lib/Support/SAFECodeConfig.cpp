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
  DPChecks("dpchecks",
           cl::init(false),
           cl::desc("Perform Dangling Pointer Checks"));

#ifdef SC_ENABLE_OOB
  cl::opt<bool>
  RewritePtrs("rewrite-oob",
              cl::init(false),
              cl::desc("Rewrite Out of Bound (OOB) Pointers"));
#else
  bool RewritePtrs = false;
#endif
  
  cl::opt<bool>
  StopOnFirstError("terminate",
                   cl::init(false),
                   cl::desc("Terminate when an Error Ocurs"));
  
  cl::opt<bool> EnableSVA("sva",
                          cl::init(false), 
                          cl::desc("Enable SVA-Kernel specific operations"));
}

namespace {
  SAFECodeConfiguration::StaticCheckTy
    none  = SAFECodeConfiguration::ABC_CHECK_NONE,
    local = SAFECodeConfiguration::ABC_CHECK_LOCAL, 
    full  = SAFECodeConfiguration::ABC_CHECK_FULL;
  
  static cl::opt<SAFECodeConfiguration::StaticCheckTy>
  StaticChecks("static-abc", cl::init(SAFECodeConfiguration::ABC_CHECK_LOCAL),
               cl::desc("Static array bounds check analysis"),
               cl::values
               (clEnumVal(none,
                          "No static array bound checks"),
                clEnumVal(local,
                          "Local static array bound checks"),
                clEnumVal(full,
                          "Omega static array bound checks"),
                clEnumValEnd));


  SAFECodeConfiguration::PATy
  single = SAFECodeConfiguration::PA_SINGLE,
    simple = SAFECodeConfiguration::PA_SIMPLE,
    multi = SAFECodeConfiguration::PA_MULTI,
    apa = SAFECodeConfiguration::PA_APA;

  static cl::opt<SAFECodeConfiguration::PATy>
  PA("pa", cl::init(simple),
     cl::desc("The type of pool allocation used by the program"),
     cl::values(
                clEnumVal(single,  "Dummy Pool Allocation (Single DS Node)"),
                clEnumVal(simple,  "Simple Pool Allocation"),
                clEnumVal(multi,   "Context-insensitive Pool Allocation"),
                clEnumVal(apa,     "Automatic Pool Allocation"),
                clEnumValEnd));
}

SAFECodeConfiguration * SCConfig;

SAFECodeConfiguration *
SAFECodeConfiguration::create() {
  return new SAFECodeConfiguration();
}

SAFECodeConfiguration::SAFECodeConfiguration() {
  // TODO: Move all cl::opt to this file and parse them here
  this->DanglingPointerChecks = DPChecks;
  this->RewriteOOB = RewritePtrs;
  this->TerminateOnErrors = StopOnFirstError;
  this->StaticCheckType = StaticChecks;
  this->PAType = PA;
  calculateDSAType();
  this->SVAEnabled = EnableSVA;
}

void
SAFECodeConfiguration::calculateDSAType() {
  struct mapping {
    PATy pa;
    DSATy dsa;
  };

  struct mapping M[] = {
    {PA_SINGLE, DSA_BASIC},
    {PA_SIMPLE, DSA_EQTD},
    {PA_MULTI,  DSA_STEENS},
    {PA_APA,    DSA_EQTD},
  };

  bool found = false;
  for (unsigned i = 0; i < sizeof(M) / sizeof(struct mapping); ++i) {
    if (PAType == M[i].pa) {
      DSAType = M[i].dsa;
      found = true;
      break;
    }
  }

  assert (found && "Inconsistent usage of Pool Allocation and DSA!");
}

NAMESPACE_SC_END
