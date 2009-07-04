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

#include "DebugReport.h"
#include "safecode/Config/config.h"

NAMESPACE_SC_BEGIN

void
DebugViolationInfo::print(std::ostream & OS) const {
  ViolationInfo::print(OS);
  OS << "Fault PC Source: " << 
    (this->SourceFile ? this->SourceFile : "<unknown>") <<
    ":" << std::dec << this->lineNo << "\n";
  if (dbgMetaData) {
    dbgMetaData->print(OS);
  }
}

void
OutOfBoundsViolation::print(std::ostream & OS) const {
  DebugViolationInfo::print(OS);
  OS << std::showbase << std::hex
     << "Object start:" << this->objStart << "\n"
     << "Object length:" << this->objLen << "\n";
}

void
DebugMetaData::print(std::ostream & OS) const {
  OS << std::showbase
     << "Object address:" << std::hex << this->canonAddr
     << "Object allocated at PC:" << std::hex << this->allocPC << "\n"
     << "Source File: " << (this->SourceFile ? this->SourceFile : "<unknown>")
     << ":" << std::dec << this->lineno
     << "Object allocation generation number:" << std::dec << this->allocID << "\n"
     << "Object freed at PC:" << std::hex << this->freePC << "\n"
     << "Object free generation number:" << std::dec << this->freeID << "\n";
}

NAMESPACE_SC_END
