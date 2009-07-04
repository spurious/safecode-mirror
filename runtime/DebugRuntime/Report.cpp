//===- Report.cpp -------------------------------------------*- C++ -*-----===//
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

#include "safecode/Runtime/Report.h"
#include "safecode/Config/config.h"

#include <iostream>

NAMESPACE_SC_BEGIN

ViolationInfo::~ViolationInfo() {}

void
ViolationInfo::print(std::ostream & OS) const {
  OS << std::showbase << std::hex 
     << "SAFECode:Violation Type " << this->type << " "
     << "when accessing  " << this->faultPtr << " "
     << "at IP=" << this->faultPC << "\n";
}

void
ReportMemoryViolation(const ViolationInfo *v) {
	v->print(std::cerr);
	abort();
}

NAMESPACE_SC_END
