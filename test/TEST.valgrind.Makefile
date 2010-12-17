##===- safecode/test/TEST.valgrind.Makefile ----------------*- Makefile -*-===##
#
# This test runs both SAFECode and valgrind on all of the Programs and produces
# performance numbers and statistics.
#
##===----------------------------------------------------------------------===##

include $(PROJ_OBJ_ROOT)/Makefile.common

#
# Turn on debug information for use with the SAFECode tool
#
CFLAGS = -g -O2 -fno-strict-aliasing

CURDIR  := $(shell cd .; pwd)
PROGDIR := $(shell cd $(LLVM_SRC_ROOT)/projects/llvm-test; pwd)/
RELDIR  := $(subst $(PROGDIR),,$(CURDIR))
GCCLD    = $(LLVM_OBJ_ROOT)/$(CONFIGURATION)/bin/gccld
WATCHDOG := $(LLVM_OBJ_ROOT)/projects/safecode/$(CONFIGURATION)/bin/watchdog
SC       := $(RUNTOOLSAFELY) $(WATCHDOG) $(LLVM_OBJ_ROOT)/projects/safecode/$(CONFIGURATION)/bin/sc -rewrite-oob
VALGRIND = valgrind -q --log-file=vglog
#VALGRIND = valgrind -q --log-file=vglog --tool=exp-ptrcheck

# Pool allocator pass shared object
PA_SO    := $(PROJECT_DIR)/Debug/lib/libaddchecks.a

# Pool allocator runtime library
#PA_RT_O  := $(PROJECT_DIR)/$(CONFIGURATION)/lib/poolalloc_safe_rt.o
PA_RT_BC := libsc_dbg_rt.bca libpoolalloc_bitmap.bca 
PA_RT_BC := $(addprefix $(PROJECT_DIR)/$(CONFIGURATION)/lib/, $(PA_RT_BC))

# Pool System library for interacting with the system
#POOLSYSTEM := $(PROJECT_DIR)/$(CONFIGURATION)/lib/UserPoolSystem.o
POOLSYSTEM :=

# SC_STATS - Run opt with the -stats and -time-passes options, capturing the
# output to a file.
SC_STATS = $(SC) -stats -time-passes -info-output-file=$(CURDIR)/$@.info

#OPTZN_PASSES := -globaldce -ipsccp -deadargelim -adce -instcombine -simplifycfg
OPTZN_PASSES := -strip-debug -std-compile-opts


#
# This rule runs SAFECode on the .llvm.bc file to produce a new .bc
# file
#
$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).bc): \
Output/%.$(TEST).bc: Output/%.llvm.bc $(PA_SO) $(LOPT)
	-@rm -f $(CURDIR)/$@.info
	-$(SC_STATS) $< -f -o $@.sc 2>&1 > $@.out
	-$(LLVMLD) -o $@.sc.ld $@.sc $(PA_RT_BC) 2>&1 > $@.out
	-$(LOPT) $(OPTZN_PASSES) $@.sc.ld.bc -o $@ -f 2>&1    >> $@.out

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.bc): \
Output/%.nonsc.bc: Output/%.llvm.bc $(LOPT)
	-@rm -f $(CURDIR)/$@.info
	-$(LOPT) $(OPTZN_PASSES) $< -o $@ -f 2>&1 > $@.out

#
# These rules compile the new .bc file into a .c file using llc
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.s): \
Output/%.safecode.s: Output/%.$(TEST).bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.s): \
Output/%.nonsc.s: Output/%.nonsc.bc $(LLC)
	-$(LLC) $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.safecode.cbe.c): \
Output/%.safecode.cbe.c: Output/%.$(TEST).bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.cbe.c): \
Output/%.nonsc.cbe.c: Output/%.nonsc.bc $(LLC)
	-$(LLC) -march=c $(LLCFLAGS) -f $< -o $@

#
# These rules compile the CBE .c file into a final executable
#
ifdef SC_USECBE
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.cbe.c $(PA_RT_O) $(POOLSYSTEM)
	-$(LLVMGCC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM) $(LDFLAGS) -o $@ -static -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.nonsc): \
Output/%.nonsc: Output/%.nonsc.cbe.c
	-$(LLVMGCC) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@
else
$(PROGRAMS_TO_TEST:%=Output/%.safecode): \
Output/%.safecode: Output/%.safecode.s $(PA_RT_O) $(POOLSYSTEM)
	-$(LLVMGCC) $(CFLAGS) $< $(LLCLIBS) $(PA_RT_O) $(POOLSYSTEM) $(LDFLAGS) -o $@ -static -lstdc++

$(PROGRAMS_TO_TEST:%=Output/%.nonsc): \
Output/%.nonsc: Output/%.nonsc.s
	-$(LLVMGCC) $(CFLAGS) $< $(LLCLIBS) $(LDFLAGS) -o $@
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
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $(WATCHDOG) $< $(RUN_OPTIONS)

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.out-llc): \
Output/%.nonsc.out-llc: Output/%.nonsc
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $(WATCHDOG) $(VALGRIND) $< $(RUN_OPTIONS)

else

#
# This rule runs the generated executable, generating timing information, for
# SPEC
#
$(PROGRAMS_TO_TEST:%=Output/%.safecode.out-llc): \
Output/%.safecode.out-llc: Output/%.safecode
	-$(SPEC_SANDBOX) safecodecbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  $(WATCHDOG) ../../$< $(RUN_OPTIONS)
	-(cd Output/safecodecbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/safecodecbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.out-llc): \
Output/%.nonsc.out-llc: Output/%.nonsc
	-$(SPEC_SANDBOX) nonsccbe-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  $(WATCHDOG) $(VALGRIND) ../../$< $(RUN_OPTIONS)
	-(cd Output/nonsccbe-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/nonsccbe-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

endif


# This rule diffs the post-poolallocated version to make sure we didn't break
# the program!
$(PROGRAMS_TO_TEST:%=Output/%.safecode.diff-llc): \
Output/%.safecode.diff-llc: Output/%.out-nat Output/%.safecode.out-llc
	@cp Output/$*.out-nat Output/$*.safecode.out-nat
	-$(DIFFPROG) llc $*.safecode $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.nonsc.diff-llc): \
Output/%.nonsc.diff-llc: Output/%.out-nat Output/%.nonsc.out-llc
	@cp Output/$*.out-nat Output/$*.nonsc.out-nat
	-$(DIFFPROG) llc $*.nonsc $(HIDEDIFF)


# This rule wraps everything together to build the actual output the report is
# generated from.
$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt): \
Output/%.$(TEST).report.txt: Output/%.out-nat                \
                             Output/%.nonsc.diff-llc         \
                             Output/%.safecode.diff-llc     \
                             Output/%.LOC.txt
	@echo > $@
	@-if test -f Output/$*.out-nat; then \
	  printf "GCC-RUN-TIME: " >> $@;\
	  grep "^program" Output/$*.out-nat.time >> $@;\
        fi
	@-if test -f Output/$*.nonsc.diff-llc; then \
	  printf "CBE-RUN-TIME-NORMAL: " >> $@;\
	  grep "^program" Output/$*.nonsc.out-llc.time >> $@;\
        fi
	@-if test -f Output/$*.safecode.diff-llc; then \
	  printf "CBE-RUN-TIME-SAFECODE: " >> $@;\
	  grep "^program" Output/$*.safecode.out-llc.time >> $@;\
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
