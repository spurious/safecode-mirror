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

LINK_COMPONENTS := bitreader bitwriter instrumentation scalaropts ipo

include $(LEVEL)/Makefile.common

$(ToolBuildPath): $(ObjectsO) $(ProjLibsPaths) $(LLVMLibsPaths) \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/convert.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/addchecks.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/speculativechecking.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/arrayboundcheck.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/pointerchecks.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/stackcheck.o \
                  $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/indirectcalls.o \
                  $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/poolalloc.o \
                  $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o
	$(Echo) Linking $(BuildMode) executable $(TOOLNAME) $(StripWarnMsg)
	$(Verb) $(LTLink) -o $@ $(TOOLLINKOPTS) $(ObjectsO) $(ProjLibsOptions) \
	$(LLVMLibsOptions) $(ExtraLibs) $(TOOLLINKOPTSB) $(LIBS)
	$(Echo) ======= Finished Linking $(BuildMode) Executable $(TOOLNAME) \
					$(StripWarnMsg)

LDFLAGS += $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/convert.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/addchecks.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/speculativechecking.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/arrayboundcheck.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/pointerchecks.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/stackcheck.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/indirectcalls.o \
           $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/poolalloc.o \
           $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o

# Include this here so we can get the configuration of the targets
# that have been configured for construction. We have to do this 
# early so we can set up USEDLIBS properly before includeing Makefile.rules

