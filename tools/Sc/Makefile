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
REQUIRES_EH := 1

LINK_COMPONENTS :=  bitreader bitwriter instrumentation scalaropts ipo \
										nativecodegen

#
# Note: pointerchecks.a has been temporarily removed from USEDLIBS.
#
#pointerchecks.a 

USEDLIBS := CStdLib.a oob.a optchecks.a \
						convert.a addchecks.a dpchecks.a speculativechecking.a \
						arrayboundcheck.a stackcheck.a \
						debuginstr.a sc-support.a scutility.a \
						LLVMDataStructure.a poolalloc.a


include $(LEVEL)/Makefile.common

#
# These rules create symbolic links from the poolalloc object tree to the
# SAFECode object tree.  This allows the LLVM build machinery to handle
# library dependencies.
#
$(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libpoolalloc.a: $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libpoolalloc.a
	$(VERB) ln -fs $< $@

$(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libLLVMDataStructure.a: $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libLLVMDataStructure.a
	$(VERB) ln -fs $< $@

