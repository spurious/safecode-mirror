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
  //
  // Print out the regular error information.
  //
  ViolationInfo::print(OS);

  //
  // Print the source filename and line number.
  //
  OS << "= Fault PC Source                       :\t"
     << (this->SourceFile ? this->SourceFile : "<unknown>")
     << ":" << std::dec << this->lineNo << "\n";

  //
  // Print the pool handle.
  //
  OS << "= Pool Handle                           :\t" << this->PoolHandle << "\n";

  //
  // Print the debug metata.
  //
  if (dbgMetaData) {
    dbgMetaData->print(OS);
  }
}

void
OutOfBoundsViolation::print(std::ostream & OS) const {
  //
  // Print out the regular error information.
  //
  DebugViolationInfo::print(OS);

  //
  // Print information on the start and end locations of the object.
  //
  OS << "= Object start                          :\t" 
     << std::showbase << std::hex << this->objStart << "\n"
     << "= Object length                         :\t"
     << this->objLen << "\n";
}

void
AlignmentViolation::print(std::ostream & OS) const {
  //
  // Print out the regular error information.
  //
  OutOfBoundsViolation::print(OS);

  //
  // Print information on the alignment requirements for the object.
  //
  OS << "= Alignment                             :\t" 
     << std::showbase << std::hex << this->alignment << "\n";
}

void
DebugMetaData::print(std::ostream & OS) const {
  OS << std::showbase
     << "= Object address:" << std::hex << this->canonAddr << "\n"
     << "= Object allocated at PC:" << std::hex << this->allocPC << "\n";
  OS << "= Source File: " << (this->SourceFile ? this->SourceFile : "<unknown>")
     << ":" << std::dec << this->lineno << "\n"
     << "= Object allocation generation number:" << std::dec
     << this->allocID << "\n"
     << "= Object freed at PC:" << std::hex << this->freePC << "\n"
     << "= Object free generation number:" << std::dec << this->freeID << "\n";
}

NAMESPACE_SC_END
