LEVEL = ../..

include $(LEVEL)/Makefile.common

CONFIGURATION	:= Debug
LLVMTOOLCURRENT := $(LLVM_OBJ_ROOT)/$(CONFIGURATION)/bin
LLVMLDPROG	:= $(LLVMTOOLCURRENT)/llvm-ld$(EXEEXT)
LLC       	:= $(LLVMTOOLCURRENT)/llc$(EXEEXT)
LOPT      	:= $(LLVMTOOLCURRENT)/opt$(EXEEXT)

SCFLAGS = -disable-staticchecks -enable-debuginfo

TEST_SRC_DIR=$(PROJ_SRC_ROOT)/test/BOdiagsuite-20050808

include $(TEST_SRC_DIR)/Makefile.rules

SRCS = $(wildcard $(TEST_SRC_DIR)/testcases/*.c)
TARGET = $(patsubst $(TEST_SRC_DIR)/testcases/%.c,%.sc,$(SRCS))

all:: $(TARGET)

%.bc : $(TEST_SRC_DIR)/testcases/%.c
	$(LLVMGCC) -O0 -c -emit-llvm $< -o $@

.PHONY:
clean::
	rm -f *.bc *.sc *.s oc.out omega_include.ip

