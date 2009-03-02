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
USEDLIBS        := faultinject

include $(LEVEL)/Makefile.common

$(ToolBuildPath): $(ObjectsO) $(ProjLibsPaths) $(LLVMLibsPaths) \
                  $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o
	$(Echo) Linking $(BuildMode) executable $(TOOLNAME) $(StripWarnMsg)
	$(Verb) $(Link) -o $@ $(TOOLLINKOPTS) $(ObjectsO) $(ProjLibsOptions) \
	$(LLVMLibsOptions) $(ExtraLibs) $(TOOLLINKOPTSB) $(LIBS)
	$(Echo) ======= Finished Linking $(BuildMode) Executable $(TOOLNAME) \
					$(StripWarnMsg)

LDFLAGS += $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o

# Include this here so we can get the configuration of the targets
# that have been configured for construction. We have to do this 
# early so we can set up USEDLIBS properly before includeing Makefile.rules

