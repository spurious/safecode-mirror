//===- SAFECodeConfig.h -----------------------------------------*- C++ -*----//
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

#ifndef _SAFECODE_CODE_CONFIG_H_
#define _SAFECODE_CODE_CONFIG_H_

#include "safecode/SAFECode.h"

NAMESPACE_SC_BEGIN

struct SAFECodeConfiguration {
  bool DanglingPointerChecks;
  bool RewriteOOB;
  bool TerminateOnErrors;

  typedef enum StaticCheckTy {
    ABC_CHECK_FULL,
    ABC_CHECK_NONE
  } StaticCheckTy;

  typedef enum DSATy {
    DSA_BASIC,
    DSA_EQTD
  } DSATy;

  StaticCheckTy StaticCheckType;
  DSATy DSAType;

  static SAFECodeConfiguration * create();
private:
  SAFECodeConfiguration();
};

extern SAFECodeConfiguration * SCConfig;
NAMESPACE_SC_END
#endif
