##===- runtime/Makefile ------------------------------------*- Makefile -*-===##
# 
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

CLANG_LEVEL := ..

LEVEL := $(CLANG_LEVEL)/../..

include $(CLANG_LEVEL)/../../Makefile.common

ifndef NO_RUNTIME_LIBS

PARALLEL_DIRS  := compiler-rt libcxx

endif

include $(CLANG_LEVEL)/Makefile

install::

