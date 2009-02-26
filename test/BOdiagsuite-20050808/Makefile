LEVEL = ../..

include $(LEVEL)/Makefile.common

CONFIGURATION	:= Debug
SHLIBEXT	:= .dylib
LLVMTOOLCURRENT := $(LLVM_OBJ_ROOT)/$(CONFIGURATION)/bin
LLVMLDPROG	:= $(LLVMTOOLCURRENT)/llvm-ld$(EXEEXT)
LLC       	:= $(LLVMTOOLCURRENT)/llc$(EXEEXT)
LOPT      	:= $(LLVMTOOLCURRENT)/opt$(EXEEXT)
LLVMGCC := llvm-gcc

SCFLAGS = -runtime=RUNTIME_DEBUG

TEST_SRC_DIR=$(PROJ_SRC_ROOT)/test/BOdiagsuite-20050808

include $(TEST_SRC_DIR)/Makefile.rules

SRCS = $(wildcard $(TEST_SRC_DIR)/testcases/*.c)
TARGET = $(patsubst $(TEST_SRC_DIR)/testcases/%.c,%.bc,$(SRCS))

all:: $(TARGET)

%.bc : $(TEST_SRC_DIR)/testcases/%.c
	$(LLVMGCC) -O0 -c -emit-llvm $< -o $@

.PHONY:
clean::
	rm $(TARGET)
