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

#LIBS := \
#poolalloc.o \
#LLVMDataStructure.o \
#convert.o \
#addchecks.o \
#arrayboundcheck.o \
#pointerchecks.o \
#stackcheck.o

LDFLAGS += $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/poolalloc.o \
           $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/LLVMDataStructure.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/convert.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/addchecks.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/arrayboundcheck.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/pointerchecks.o \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/stackcheck.o

# Include this here so we can get the configuration of the targets
# that have been configured for construction. We have to do this 
# early so we can set up USEDLIBS properly before includeing Makefile.rules
include $(LEVEL)/Makefile.common


