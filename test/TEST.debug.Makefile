##===- TEST.debug.Makefile ---------------------------------*- Makefile -*-===##
#
# This test runs performance experiments using SAFECode as a debugging tool.
#
##===----------------------------------------------------------------------===##

include $(PROJ_OBJ_ROOT)/Makefile.common

ifndef ENABLE_LTO
ENABLE_LTO := 1
endif

CFLAGS := -g -O2 -fno-strict-aliasing

CURDIR  := $(shell cd .; pwd)
PROGDIR := $(shell cd $(LLVM_SRC_ROOT)/projects/llvm-test; pwd)/
RELDIR  := $(subst $(PROGDIR),,$(CURDIR))
GCCLD    = $(LLVM_OBJ_ROOT)/$(CONFIGURATION)/bin/gccld
SCOPTS  := -enable-debuginfo -terminate -check-every-gep-use -disable-structchecks=false
SC      := $(LLVM_OBJ_ROOT)/projects/safecode/$(CONFIGURATION)/bin/sc

# Pool allocator pass shared object
PA_SO    := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libaddchecks.a

# Pool allocator runtime library
PA_RT_O  :=
PA_RT_BC :=

# Pool System library for interacting with the system
POOLSYSTEM_RT_O :=
POOLSYSTEM_RT_BC :=

#Pre runtime of Pool allocator
PA_PRE_RT_BC :=
PA_PRE_RT_O  := 

ifeq ($(ENABLE_LTO),1)
PA_RT_BC := libsc_dbg_rt.bca libpoolalloc_bitmap.bca 
POOLSYSTEM_RT_BC := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libUserPoolSystem.bca
# Bits of runtime to improve analysis
PA_PRE_RT_BC := $(POOLALLOC_OBJDIR)/$(CONFIGURATION)/lib/libpa_pre_rt.bca
else
PA_RT_O  := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libsc_dbg_rt.a \
            $(PROJECT_DIR)/$(CONFIGURATION)/lib/libpoolalloc_bitmap.a
POOLSYSTEM_RT_O := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libUserPoolSystem.a

# TODO: Test whether it works
#PA_PRE_RT_O := $(POOLALLOC_OBJDIR)/$(CONFIGURATION)/lib/libpa_pre_rt.o
endif

PA_RT_BC := $(addprefix $(PROJECT_DIR)/$(CONFIGURATION)/lib/, $(PA_RT_BC))

# SC_STATS - Run opt with the -stats and -time-passes options, capturing the
# output to a file.
SC_STATS = $(SC) -stats -time-passes -info-output-file=$(CURDIR)/$@.info

# Pre processing library for DSA
ASSIST_SO := $(POOLALLOC_OBJDIR)/$(CONFIGURATION)/lib/libAssistDS$(SHLIBEXT)

PRE_SC_OPT_FLAGS = -load $(ASSIST_SO) -instnamer -internalize -indclone -funcspec -ipsccp -deadargelim -instcombine -globaldce -licm

EXTRA_LOPT_OPTIONS :=
#-loopsimplify -unroll-threshold 0 
OPTZN_PASSES := -strip-debug -std-compile-opts $(EXTRA_LOPT_OPTIONS)
#EXTRA_LINKTIME_OPT_FLAGS = $(EXTRA_LOPT_OPTIONS) 

ifeq ($(OS),Darwin)
LDFLAGS += -lpthread
else
LDFLAGS += -lrt -lpthread
endif

# DEBUGGING
#   o) Don't add -g to CFLAGS, CXXFLAGS, or CPPFLAGS; these are used by the
#      rules to compile code with llvm-gcc and enable LLVM debug information;
#      this, in turn, causes test cases to fail unnecessairly.
#CBECFLAGS += -g
#LLVMLDFLAGS= -disable-opt

#
# This rule links in part of the SAFECode run-time with the original program.
#
$(PROGRAMS_TO_TEST:%=Output/%.presc.bc): \
Output/%.presc.bc: Output/%.llvm.bc $(LOPT) $(PA_PRE_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -link-as-library -o $@.paprert.bc $< $(PA_PRE_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(PRE_SC_OPT_FLAGS) $@.paprert.bc -f -o $@ 2>&1 > $@.out

#
# Create a SAFECode executable without pointer rewriting.
#
$(PROGRAMS_TO_TEST:%=Output/%.noOOB.bc): \
Output/%.noOOB.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCOPTS) $< -f -o $@.noOOB 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.noOOB.ld $@.noOOB $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.noOOB.ld.bc -o $@ -f 2>&1    >> $@.out

#
# Create a SAFECode executable with pointer rewriting.
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.bc): \
Output/%.safecode.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCOPTS) -rewrite-oob $< -f -o $@.sc 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.sc.ld $@.sc $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.sc.ld.bc -o $@ -f 2>&1    >> $@.out

#
# These rules compile the new .bc file into a .c file using llc
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.s): \
Output/%.safecode.s: Output/%.safecode.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.noOOB.s): \
Output/%.noOOB.s: Output/%.noOOB.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.safecode.cbe.c): \
Output/%.safecode.cbe.c: Output/%.safecode.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.noOOB.cbe.c): \
Output/%.noOOB.cbe.c: Output/%.noOOB.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

#
# These rules compile the CBE .c file into a final executable
#
ifdef SC_USECBE
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.cbe.c $(PA_RT_O) $(POOLSYSTEM_RT_O)
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.noOOB): \
Output/%.noOOB: Output/%.noOOB.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++
else
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.s $(PA_RT_O) $(POOLSYSTEM_RT_O)
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.noOOB): \
Output/%.noOOB: Output/%.noOOB.s
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++
endif

##############################################################################
# Rules for running executables and generating reports
##############################################################################

ifndef PROGRAMS_HAVE_CUSTOM_RUN_RULES

#
# This rule runs the generated executable, generating timing information, for
# normal test programs
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.out-llc): \
Output/%.safecode.out-llc: Output/%.safecode
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

$(PROGRAMS_TO_TEST:%=Output/%.noOOB.out-llc): \
Output/%.noOOB.out-llc: Output/%.noOOB
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

else

#
# This rule runs the generated executable, generating timing information, for
# SPEC
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.out-llc): \
Output/%.safecode.out-llc: Output/%.safecode
	-$(SPEC_SANDBOX) safecodecbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/safecodecbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/safecodecbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.noOOB.out-llc): \
Output/%.noOOB.out-llc: Output/%.noOOB
	-$(SPEC_SANDBOX) noOOBcbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/noOOBcbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/noOOBcbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

endif


# This rule diffs the post-poolallocated version to make sure we didn't break
# the program!
$(PROGRAMS_TO_TEST:%=Output/%.safecode.diff-llc): \
Output/%.safecode.diff-llc: Output/%.out-nat Output/%.safecode.out-llc
	@cp Output/$*.out-nat Output/$*.safecode.out-nat
	-$(DIFFPROG) llc $*.safecode $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.noOOB.diff-llc): \
Output/%.noOOB.diff-llc: Output/%.out-nat Output/%.noOOB.out-llc
	@cp Output/$*.out-nat Output/$*.noOOB.out-nat
	-$(DIFFPROG) llc $*.noOOB $(HIDEDIFF)


# This rule wraps everything together to build the actual output the report is
# generated from.
$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt): \
Output/%.$(TEST).report.txt: Output/%.out-nat                \
                             Output/%.noOOB.diff-llc         \
                             Output/%.safecode.diff-llc     \
                             Output/%.LOC.txt
	@echo > $@
	@-if test -f Output/$*.out-nat; then \
	  printf "GCC-RUN-TIME: " >> $@;\
	  grep "^real" Output/$*.out-nat.time >> $@;\
        fi
	@-if test -f Output/$*.noOOB.diff-llc; then \
	  printf "CBE-RUN-TIME-NORMAL: " >> $@;\
	  grep "^real" Output/$*.noOOB.out-llc.time >> $@;\
        fi
	@-if test -f Output/$*.safecode.diff-llc; then \
	  printf "CBE-RUN-TIME-SAFECODE: " >> $@;\
	  grep "^real" Output/$*.safecode.out-llc.time >> $@;\
	fi
	printf "LOC: " >> $@
	cat Output/$*.LOC.txt >> $@
	#@cat Output/$*.$(TEST).bc.info >> $@

$(PROGRAMS_TO_TEST:%=test.$(TEST).%): \
test.$(TEST).%: Output/%.$(TEST).report.txt
	@echo "---------------------------------------------------------------"
	@echo ">>> ========= '$(RELDIR)/$*' Program"
	@echo "---------------------------------------------------------------"
	@cat $<

REPORT_DEPENDENCIES := $(PA_RT_O) $(PA_SO) $(PROGRAMS_TO_TEST:%=Output/%.llvm.bc) $(LLC) $(LOPT)
