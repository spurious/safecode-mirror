#===- tools/InjectF/Makefile -------------------------------*- Makefile -*-===##
# 
#                           SAFECode Compiler Project
#
# This file was developed by the LLVM research group and is distributed under
# the University of Illinois Open Source License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LEVEL = ../..
TOOLNAME=injectf

# Initialize the USEDLIBS so we can add to it

LINK_COMPONENTS := bitreader bitwriter instrumentation scalaropts ipo
USEDLIBS        := faultinject.a

include $(LEVEL)/Makefile.common

LDFLAGS += $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o

