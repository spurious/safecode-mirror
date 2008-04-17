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

LDFLAGS += -Wl,-rpath $(POOLALLOC_OBJDIR)/$(BuildMode)/lib \
           -Wl,-rpath $(PROJ_OBJ_ROOT)/$(BuildMode)/lib \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libconvert$(SHLIBEXT) \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libaddchecks$(SHLIBEXT) \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libarrayboundcheck$(SHLIBEXT) \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libpointerchecks$(SHLIBEXT) \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libstackcheck$(SHLIBEXT) \
           $(PROJ_OBJ_ROOT)/$(BuildMode)/lib/libindirectcalls$(SHLIBEXT) \
           $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libpoolalloc$(SHLIBEXT) \
           $(POOLALLOC_OBJDIR)/$(BuildMode)/lib/libLLVMDataStructure$(SHLIBEXT)

# Include this here so we can get the configuration of the targets
# that have been configured for construction. We have to do this 
# early so we can set up USEDLIBS properly before includeing Makefile.rules
include $(LEVEL)/Makefile.common


