##===- safecode/test/Makefile ------------------------------*- Makefile -*-===##
#
# Define the default test rule, which allows people to just type 'make test' in
# this directory.
#
##===----------------------------------------------------------------------===##

# The default target in this directory is the test:: target
test::

LEVEL = ..
include $(LEVEL)/Makefile.common

ifndef NO_STABLE_NUMBERS
STABLERUN := GET_STABLE_NUMBERS=1
else
STABLERUN :=
endif

ifndef NO_LARGE_SIZE
LARGESIZE := LARGE_PROBLEM_SIZE=1
else
LARGESIZE :=
endif

ifdef ENABLE_OPTIMIZED
OPTIMIZED := ENABLE_OPTIMIZED=1
else
OPTIMIZED :=
endif

RUNTIMELIMIT=3600

PROFILING_DAT_DIR := $(shell pwd)/data
PROFILING_RESULT_DIR := $(shell pwd)/data/profiling

LARGE_PROBLEM_SIZE_DIRS :=


LARGE_PROBLEM_SIZE_DIRS := \
   $(addprefix $(TEST_OBJDIR)/,$(LARGE_PROBLEM_SIZE_DIRS))

#DEBUG_SAFECODE := DEBUG_SAFECODE=1

NORMAL_PROBLEM_SIZE_DIRS := \
  MultiSource/Benchmarks/Olden \
  External/SPEC/CINT2000 \
  #MultiSource/Benchmarks/Olden/bisort \
  #External/SPEC/CINT2000/197.parser \
  #External/SPEC/CINT2000/164.gzip \
  #External/SPEC/CINT2000/256.bzip2 \
  #External/Povray \
  #MultiSource/Benchmarks/Olden/tsp \
  #SingleSource/Benchmarks \
  #MultiSource/Benchmarks/Olden/bh \
  #MultiSource/Benchmarks/Olden/em3d \
  #External/SPEC/CINT2000/181.mcf \
  #External/SPEC/CINT2000/254.gap \
  #External/SPEC/CINT2000/300.twolf \
  #External/SPEC/CINT2000/175.vpr \
  #External/SPEC/CINT2000/186.crafty \
  #MultiSource/Benchmarks/Olden/mst \
  #External/SPEC/CINT2000/197.parser \
  #MultiSource/Benchmarks/Olden/power \
  #External/SPEC/CINT2000/253.perlbmk \
  #External/SPEC/CINT2000/252.eon \
  #External/SPEC/CINT2000/186.crafty \
  #External/SPEC/CINT2000/181.mcf \
  #External/SPEC/CINT2000/164.gzip \
  #External/SPEC/CINT2000/255.vortex \
  #External/SPEC/CINT2000/175.vpr \
  #External/SPEC/CINT2000/186.crafty \
  #External/SPEC/CINT2000/254.gap \
  ## WORKING: \
  # \
  #External/SPEC/CINT2000/164.gzip \
  #External/SPEC/CINT2000/197.parser \
  ## FULL LIST: \
  # \
  #MultiSource/Benchmarks/Olden \
  #External/SPEC/CINT2000 \
  #External/SPEC/CINT2000/181.mcf \
  #MultiSource/Benchmarks/Olden/voronoi \
  #External/SPEC/CINT2000/176.gcc \
  #External/Namd \
  #External/SPEC/CINT2000/175.vpr \
  #External/SPEC/CINT2000 \
  #External/SPEC/CINT2000/252.eon \
  #MultiSource/Applications/Burg \
  #MultiSource/Benchmarks/Olden/health \
  #MultiSource/Benchmarks/Olden/perimeter \
  #MultiSource/Benchmarks/Olden/treeadd \
#  MultiSource/Benchmarks/Olden \
#  MultiSource/Benchmarks/Olden/em3d \
#   MultiSource/Benchmarks/Olden \
#   MultiSource/Benchmarks/Ptrdist \
#   SingleSource \
#   External/SPEC/CINT2000/300.twolf \
#   SingleSource \
#   MultiSource \
#   MultiSource/Benchmarks \
#   External/SPEC/CINT2000 \
   #External/SPEC/CFP2000/188.ammp \
   #MultiSource/Applications/ghttpd \
   #MultiSource/Benchmarks/Olden/bh \
   #MultiSource/Benchmarks/Ptrdist \
   #MultiSource/Applications \
   #MultiSource/Benchmarks


NORMAL_PROBLEM_SIZE_DIRS := \
   $(addprefix $(TEST_OBJDIR)/,$(NORMAL_PROBLEM_SIZE_DIRS))

##############################################################################
# Targets for gathering statistics for programs for papers
##############################################################################
               
# Program tests for Parallel Checking
partest::
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=par \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv; \
			mkdir -p $(PROFILING_RESULT_DIR)/`basename $$dir`; \
			mv $(PROFILING_DAT_DIR)/profiler.*.dat $(PROFILING_RESULT_DIR)/`basename $$dir` || true;) \
			done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=par \
                   report) \
        done

# Program tests for Pool Allocation
progtest::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=safecode \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               LARGE_PROBLEM_SIZE=1 \
               report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=safecode \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv; \
mkdir -p $(PROFILING_RESULT_DIR)/`basename $$dir`; \
mv $(PROFILING_DAT_DIR)/profiler.*.dat $(PROFILING_RESULT_DIR)/`basename $$dir` || true;) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir;  \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=safecode \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=safecode \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

# Program tests for SAFECode debug tool
progdebug::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=debug \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               LARGE_PROBLEM_SIZE=1 \
               report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=debug \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) $(DEBUG_SAFECODE) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv; \
      ) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir;  \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=debug \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=debug \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progbb::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=bb \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               LARGE_PROBLEM_SIZE=1 \
               report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=bb \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) $(DEBUG_SAFECODE) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv; \
      ) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir;  \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=bb \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=bb \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

# Program tests for measuring all the different SAFECode overhead
progfull::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=full \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               LARGE_PROBLEM_SIZE=1 \
               report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=full \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv;) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir;  \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=full \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=full \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progabc::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=abc \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) LARGE_PROBLEM_SIZE=1 report.html) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=abc \
                   report.html) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=abc \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=abc \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

# Run tests comparing SAFECode and Valgrind
progvg::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=valgrind \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
               LARGE_PROBLEM_SIZE=1\
               report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) TEST=valgrind \
               RUNTIMELIMIT=$(RUNTIMELIMIT) \
                   report.html report.csv) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=valgrind \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s TEST=valgrind \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progperf::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=perf \
                   LARGE_PROBLEM_SIZE=1 report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=perf \
                   report.html report.csv) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=perf \
                   LARGE_PROBLEM_SIZE=1 report report.csv) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=perf \
                   report report.csv) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progp4perf::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=p4perf \
                   LARGE_PROBLEM_SIZE=1 report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=p4perf \
                   report.html report.csv) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=p4perf \
                   LARGE_PROBLEM_SIZE=1 report report.csv) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=p4perf \
                   report report.csv) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progcputrack::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=cputrack \
                   LARGE_PROBLEM_SIZE=1 report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=cputrack \
                   report.html report.csv) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=cputrack \
                   LARGE_PROBLEM_SIZE=1 report report.csv) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=cputrack \
                   report report.csv) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"


##############################################################################
# Targets for running tests and gathering statistics for arbitrary tests
##############################################################################

# test target - Descend into projects/test-suite and run the TEST.safecode.Makefile
# tests...
test::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=safecode \
                   $(LARGESIZE) $(STABLERUN) report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

pacompiletime::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=pacompiletime \
                   report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

ptrcomp::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=ptrcomp \
                   $(LARGESIZE) $(STABLERUN) report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

ptrcomptest::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=ptrcomp \
                   $(LARGESIZE) report)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

vtl::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=pavtl \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

perf::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=perf \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

optzn::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=optzn \
                   $(LARGESIZE) $(STABLERUN) test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

p4perf::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=p4perf \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

strace::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=strace \
                   test)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

cputrack::
	(cd $(TEST_OBJDIR)/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=cputrack \
                   report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

##===----------------------------------------------------------------------===##
# Lit tests
##===----------------------------------------------------------------------===##

.PHONY: lit litclean

ifeq ($(ENABLE_OPTIMIZED),1)
CONFIGURATION := Release
else
CONFIGURATION := Debug
endif

# Path to SAFECode libraries
SC_LIB  := $(PROJ_OBJ_ROOT)/$(CONFIGURATION)/lib
SC_BIN  := $(PROJ_OBJ_ROOT)/$(CONFIGURATION)/bin/sc

SETPATH = \
	PATH=$(PROJ_OBJ_ROOT)/test/cstdlib:$(PATH)

TESTSUITE=$(PROJ_SRC_ROOT)/test/cstdlib

# Run the lit tests for cstdlib
lit: cstdlib/test.sh
	$(SETPATH) $(MAKE) -C $(LLVM_OBJ_ROOT)/test check-local-lit TESTSUITE=$(TESTSUITE)
	@rmdir $(TESTSUITE)/Output

cstdlib/test.sh: $(PROJ_SRC_ROOT)/test/cstdlib/test.sh.in
	@echo Creating test script...
	@mkdir cstdlib
	@sed -e 's#@LLVMGCC@#$(LLVMGCC)#g' \
       -e 's#@SC@#$(SC_BIN)#g' \
       -e 's#@LLC@#$(LLC)#g' \
       -e 's#@SC_LIB@#$(SC_LIB)#g' < $< > $@

litclean:
	rm -f cstdlib/test.sh
