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

#include <vector>

NAMESPACE_SC_BEGIN

/// Forward declaration
class AllocatorInfo;

struct SAFECodeConfiguration {
  bool DanglingPointerChecks;
  bool RewriteOOB;
  bool TerminateOnErrors;
  bool SVAEnabled;

  typedef enum StaticCheckTy {
    ABC_CHECK_FULL,
    ABC_CHECK_LOCAL,
    ABC_CHECK_NONE
  } StaticCheckTy;

  typedef enum DSATy {
    DSA_BASIC,
    DSA_EQTD
  } DSATy;

  StaticCheckTy StaticCheckType;
  DSATy DSAType;

  typedef std::vector<AllocatorInfo* > AllocatorInfoListTy;
  typedef AllocatorInfoListTy::iterator alloc_iterator;
  AllocatorInfoListTy allocators;
  alloc_iterator alloc_begin() { return allocators.begin(); }
  alloc_iterator alloc_end() { return allocators.end(); }

  static SAFECodeConfiguration * create();
private:
  SAFECodeConfiguration();
};

extern SAFECodeConfiguration * SCConfig;

NAMESPACE_SC_END
#endif
