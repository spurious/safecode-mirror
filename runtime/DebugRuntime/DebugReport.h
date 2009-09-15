//===- Report.h - Debugging reports for bugs found by SAFECode -*- C++ -*--===//
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
//
//===----------------------------------------------------------------------===//

#ifndef _DEBUG_REPORT_H_
#define _DEBUG_REPORT_H_

#include "SafeCodeRuntime.h"
#include "safecode/Runtime/Report.h"

NAMESPACE_SC_BEGIN

struct DebugViolationInfo : public ViolationInfo {
  const DebugMetaData * dbgMetaData;
	const void * PoolHandle;
  const char * SourceFile;
  unsigned int lineNo;
  virtual void print (std::ostream & OS) const;
  DebugViolationInfo() : dbgMetaData(0), SourceFile(0), lineNo(0) {}
};

struct OutOfBoundsViolation : public DebugViolationInfo {
  //  objstart - The start of the object in which the source pointer was found.
  //  objlen   - The length of the object in which the source pointer was found.
  const void * objStart;
  unsigned int objLen;
  virtual void print (std::ostream & OS) const;
};

struct AlignmentViolation : public OutOfBoundsViolation {
  unsigned int alignment;
  virtual void print (std::ostream & OS) const;
};

NAMESPACE_SC_END

#endif
