##===- TEST.full.Makefile ----------------------------------*- Makefile -*-===##
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
SC      := $(LLVM_OBJ_ROOT)/projects/safecode/$(CONFIGURATION)/bin/sc

#
# Various SC options:
#   SCOPTS: SAFECode options common to all experimental runs
#
SCOPTS  := -disable-staticchecks -terminate
SCOOB   := $(SCOPTS) -rewrite-oob -disable-debuginfo
SCDEBUG := $(SCOPTS) -rewrite-oob
SCPA    := $(SCOPTS) -rewrite-oob -pa -disable-debuginfo
SCDP    := $(SCOPTS) -rewrite-oob -dpchecks -disable-debuginfo

# Pool allocator pass shared object
PA_SO    := $(PROJECT_DIR)/$(CONFIGURATION)/lib/addchecks.o

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
PA_RT_BC := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libpoolalloc_safe_rt.bca
POOLSYSTEM_RT_BC := $(PROJECT_DIR)/$(CONFIGURATION)/lib/libUserPoolSystem.bca
# Bits of runtime to improve analysis
PA_PRE_RT_BC := $(POOLALLOC_OBJDIR)/$(CONFIGURATION)/lib/libpa_pre_rt.bca
else
PA_RT_O  := $(PROJECT_DIR)/$(CONFIGURATION)/lib/poolalloc_safe_rt.o
POOLSYSTEM_RT_O := $(PROJECT_DIR)/$(CONFIGURATION)/lib/UserPoolSystem.o

# TODO: Test whether it works
#PA_PRE_RT_O := $(POOLALLOC_OBJDIR)/$(CONFIGURATION)/lib/libpa_pre_rt.o
endif

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
# Create a SAFECode executable with just the "regular" options
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.bc): \
Output/%.safecode.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCOPTS) $< -f -o $@.safecode 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.safecode.ld $@.safecode $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.safecode.ld.bc -o $@ -f 2>&1    >> $@.out

#
# Create a SAFECode executable with pointer rewriting.
#
$(PROGRAMS_TO_TEST:%=Output/%.oob.bc): \
Output/%.oob.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCOOB) $< -f -o $@.oob 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.oob.ld $@.oob $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.oob.ld.bc -o $@ -f 2>&1    >> $@.out

#
# Create a SAFECode executable using real pool allocation.
#
$(PROGRAMS_TO_TEST:%=Output/%.scpa.bc): \
Output/%.scpa.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCPA) $< -f -o $@.scpa 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.scpa.ld $@.scpa $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.scpa.ld.bc -o $@ -f 2>&1    >> $@.out

#
# Create a SAFECode executable with debug information enabled.
#
$(PROGRAMS_TO_TEST:%=Output/%.scdebug.bc): \
Output/%.scdebug.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCDEBUG) $< -f -o $@.scdebug 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.scdebug.ld $@.scdebug $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.scdebug.ld.bc -o $@ -f 2>&1    >> $@.out

#
# Create a SAFECode executable with dangling pointer detection enabled.
#
$(PROGRAMS_TO_TEST:%=Output/%.dp.bc): \
Output/%.dp.bc: Output/%.presc.bc $(LOPT) $(PA_RT_BC) $(POOLSYSTEM_RT_BC)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $(SCDEBUG) $< -f -o $@.dp 2>&1 > $@.out
	-$(LLVMLDPROG) $(LLVMLDFLAGS) -o $@.dp.ld $@.dp $(PA_RT_BC) $(POOLSYSTEM_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.dp.ld.bc -o $@ -f 2>&1    >> $@.out

#
# These rules compile the new .bc file into a .c file using llc
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.s): \
Output/%.safecode.s: Output/%.safecode.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.oob.s): \
Output/%.oob.s: Output/%.oob.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.scpa.s): \
Output/%.scpa.s: Output/%.scpa.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.scdebug.s): \
Output/%.scdebug.s: Output/%.scdebug.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.dp.s): \
Output/%.dp.s: Output/%.dp.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.safecode.cbe.c): \
Output/%.safecode.cbe.c: Output/%.safecode.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.oob.cbe.c): \
Output/%.oob.cbe.c: Output/%.oob.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.scpa.cbe.c): \
Output/%.scpa.cbe.c: Output/%.scpa.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.scdebug.cbe.c): \
Output/%.scdebug.cbe.c: Output/%.scdebug.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.dp.cbe.c): \
Output/%.dp.cbe.c: Output/%.dp.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

#
# These rules compile the CBE .c file into a final executable
#
ifdef SC_USECBE
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.oob): \
Output/%.oob: Output/%.oob.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.scpa): \
Output/%.scpa: Output/%.scpa.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.scdebug): \
Output/%.scdebug: Output/%.scdebug.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.dp): \
Output/%.dp: Output/%.dp.cbe.c
	-$(CC) $(CBECFLAGS) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@ -lstdc++

else
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.s
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.oob): \
Output/%.oob: Output/%.oob.s
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.scpa): \
Output/%.scpa: Output/%.scpa.s
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.scdebug): \
Output/%.scdebug: Output/%.scdebug.s
	-$(CC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM_RT_O) $(LDFLAGS) -o $@ -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.dp): \
Output/%.dp: Output/%.dp.s
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

$(PROGRAMS_TO_TEST:%=Output/%.oob.out-llc): \
Output/%.oob.out-llc: Output/%.oob
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

$(PROGRAMS_TO_TEST:%=Output/%.scpa.out-llc): \
Output/%.scpa.out-llc: Output/%.scpa
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

$(PROGRAMS_TO_TEST:%=Output/%.scdebug.out-llc): \
Output/%.scdebug.out-llc: Output/%.scdebug
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

$(PROGRAMS_TO_TEST:%=Output/%.dp.out-llc): \
Output/%.dp.out-llc: Output/%.dp
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

else

#
# This rule runs the generated executable, generating timing information, for
# SPEC
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.out-llc): \
Output/%.safecode.out-llc: Output/%.safecode
	-$(SPEC_SANDBOX) safecode.cbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/safecode.cbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/safecode.cbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.oob.out-llc): \
Output/%.oob.out-llc: Output/%.oob
	-$(SPEC_SANDBOX) oob.cbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/oob.cbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/oob.cbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.scpa.out-llc): \
Output/%.scpa.out-llc: Output/%.scpa
	-$(SPEC_SANDBOX) scpa.cbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/scpa.cbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/scpa.cbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.scdebug.out-llc): \
Output/%.scdebug.out-llc: Output/%.scdebug
	-$(SPEC_SANDBOX) scdebug.cbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/scdebug.cbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/scdebug.cbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.dp.out-llc): \
Output/%.dp.out-llc: Output/%.dp
	-$(SPEC_SANDBOX) dp.cbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/dp.cbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/dp.cbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time
endif

# This rule diffs the post-poolallocated version to make sure we didn't break
# the program!
$(PROGRAMS_TO_TEST:%=Output/%.safecode.diff-sc): \
Output/%.safecode.diff-sc: Output/%.out-nat Output/%.safecode.out-llc
	cp Output/$*.out-nat Output/$*.safecode.out-nat
	-$(DIFFPROG) llc $*.safecode $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.oob.diff-sc): \
Output/%.oob.diff-sc: Output/%.out-nat Output/%.oob.out-llc
	@cp Output/$*.out-nat Output/$*.oob.out-nat
	-$(DIFFPROG) llc $*.oob $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.scpa.diff-sc): \
Output/%.scpa.diff-sc: Output/%.out-nat Output/%.scpa.out-llc
	@cp Output/$*.out-nat Output/$*.scpa.out-nat
	-$(DIFFPROG) llc $*.scpa $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.scdebug.diff-sc): \
Output/%.scdebug.diff-sc: Output/%.out-nat Output/%.scdebug.out-llc
	@cp Output/$*.out-nat Output/$*.scdebug.out-nat
	-$(DIFFPROG) llc $*.scdebug $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.dp.diff-sc): \
Output/%.dp.diff-sc: Output/%.out-nat Output/%.dp.out-llc
	@cp Output/$*.out-nat Output/$*.dp.out-nat
	-$(DIFFPROG) llc $*.dp $(HIDEDIFF)

#$(PROGRAMS_TO_TEST:%=Output/%.noOOB.diff-llc): \
#Output/%.noOOB.diff-llc: Output/%.out-nat Output/%.noOOB.out-llc
#	@cp Output/$*.out-nat Output/$*.noOOB.out-nat
#	-$(DIFFPROG) llc $*.noOOB $(HIDEDIFF)

# This rule wraps everything together to build the actual output the report is
# generated from.
$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt): \
Output/%.$(TEST).report.txt: Output/%.out-nat                \
                             Output/%.out-llc         \
                             Output/%.safecode.diff-sc     \
                             Output/%.oob.diff-sc         \
                             Output/%.scpa.diff-sc         \
                             Output/%.scdebug.diff-sc         \
                             Output/%.dp.diff-sc         \
                             Output/%.LOC.txt
	@echo > $@
	@-if test -f Output/$*.out-nat; then \
	  printf "GCC-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.out-nat.time >> $@;\
  fi
	@-if test -f Output/$*.out-llc; then \
	  printf "LLVM-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.out-nat.time >> $@;\
  fi
	@-if test -f Output/$*.safecode.diff-llc; then \
	  printf "SC-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.safecode.out-llc.time >> $@;\
  fi
	@-if test -f Output/$*.oob.diff-llc; then \
	  printf "OOB-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.oob.out-llc.time >> $@;\
	fi
	@-if test -f Output/$*.scpa.diff-llc; then \
	  printf "SCPA-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.scpa.out-llc.time >> $@;\
	fi
	@-if test -f Output/$*.scdebug.diff-llc; then \
	  printf "SCDEBUG-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.scdebug.out-llc.time >> $@;\
	fi
	@-if test -f Output/$*.dp.diff-llc; then \
	  printf "SCDP-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.dp.out-llc.time >> $@;\
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
