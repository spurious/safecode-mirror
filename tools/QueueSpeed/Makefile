#===- tools/InjectF/Makefile -------------------------------*- Makefile -*-===##
# 
#                           SAFECode Compiler Project
#
# This file was developed by the LLVM research group and is distributed under
# the University of Illinois Open Source License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LEVEL = ../..
TOOLNAME=queue_speed

USEDLIBS        := poolalloc_safe_rt

include $(LEVEL)/Makefile.common

lqs:
	llvm-g++ -O3 -emit-llvm -c queue_speed.cpp -I /home/andrewl/Research/llvm/include -I  /home/andrewl/Research/llvm/projects/safecode/include -I /home/andrewl/Research/llvm/projects/poolalloc/include
	llvm-ld	queue_speed.o ../../Release/lib/libpoolalloc_safe_rt.bca -o lqs
	rm lqs
	llc lqs.bc -f
	g++ lqs.s -o lqs -lpthread
