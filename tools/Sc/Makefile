#===- tools/sc/Makefile ------------------------------------*- Makefile -*-===##
# 
#                     The LLVM Compiler Infrastructure
#
# This file was developed by the LLVM research group and is distributed under
# the University of Illinois Open Source License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LEVEL = ../..
TOOLNAME=sc

# Initialize the USEDLIBS so we can add to it

LINK_COMPONENTS :=  bitreader bitwriter instrumentation scalaropts ipo \
										nativecodegen

USEDLIBS := CStdLib.a faultinject.a oob.a optchecks.a \
						convert.a addchecks.a speculativechecking.a \
						arrayboundcheck.a stackcheck.a \
						indirectcalls.a debuginstr.a sc-support.a scutility.a
#pointerchecks.a 

include $(LEVEL)/Makefile.common

LIBS += $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libpoolalloc.a \
        $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libLLVMDataStructure.a \
        -lLLVMSupport -lLLVMSystem

