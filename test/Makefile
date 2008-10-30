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

PROFILING_DAT_DIR := /Users/mai4/work/data
PROFILING_RESULT_DIR := /Users/mai4/work/data/profiling

LARGE_PROBLEM_SIZE_DIRS :=


LARGE_PROBLEM_SIZE_DIRS := \
   $(addprefix $(LLVM_OBJ_ROOT)/projects/llvm-test/,$(LARGE_PROBLEM_SIZE_DIRS))


NORMAL_PROBLEM_SIZE_DIRS := \
  MultiSource/Benchmarks/Olden \
  #MultiSource/Benchmarks/Olden/bh \
  MultiSource/Benchmarks/Olden/bisort \
  MultiSource/Benchmarks/Olden/health \
  MultiSource/Benchmarks/Olden/mst \
  MultiSource/Benchmarks/Olden/perimeter \
  MultiSource/Benchmarks/Olden/power \
  MultiSource/Benchmarks/Olden/treeadd \
  MultiSource/Benchmarks/Olden/tsp \
#  MultiSource/Benchmarks/Olden \
#  MultiSource/Benchmarks/Olden/em3d \
#  MultiSource/Benchmarks/Olden/voronoi \
#   MultiSource/Benchmarks/Olden \
#   MultiSource/Benchmarks/Ptrdist \
#   External/SPEC/CINT2000 \
#   External/SPEC/CINT2000/164.gzip \
#   SingleSource \
#   External/SPEC/CINT2000/300.twolf \
#   SingleSource \
#   MultiSource \
#   MultiSource/Benchmarks \
#   External/SPEC/CINT2000 \
#   External/SPEC/CINT2000/181.mcf \
#   External/SPEC/CINT2000/256.bzip2 \
#   External/Povray \
   #External/SPEC/CINT2000/186.crafty \
   #External/SPEC/CFP2000/188.ammp \
   #External/Namd \
   #MultiSource/Applications/ghttpd \
   #MultiSource/Benchmarks/Olden/bh \
   #MultiSource/Benchmarks/Ptrdist \
   #MultiSource/Applications \
   #MultiSource/Benchmarks


NORMAL_PROBLEM_SIZE_DIRS := \
   $(addprefix $(LLVM_OBJ_ROOT)/projects/llvm-test/,$(NORMAL_PROBLEM_SIZE_DIRS))

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
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=safecode \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) LARGE_PROBLEM_SIZE=1 report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=safecode \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) \
               $(LARGESIZE) $(STABLERUN) $(OPTIMIZED) \
                   report.html report.csv; \
mkdir -p $(PROFILING_RESULT_DIR)/`basename $$dir`; \
mv $(PROFILING_DAT_DIR)/profiler.*.dat $(PROFILING_RESULT_DIR)/`basename $$dir` || true;) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir;  \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=safecode \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=safecode \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

progabc::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=abc \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) LARGE_PROBLEM_SIZE=1 report.html) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=abc \
                   report.html) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=abc \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=abc \
                   report) \
        done
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

# Run tests comparing SAFECode and Valgrind
progvg::
	for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=valgrind \
               POOLALLOC_OBJDIR=$(POOLALLOC_OBJDIR) LARGE_PROBLEM_SIZE=1 report.html report.csv) \
        done
	for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=valgrind \
                   report.html report.csv) \
        done
	@for dir in $(LARGE_PROBLEM_SIZE_DIRS); do \
            (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=valgrind \
                   LARGE_PROBLEM_SIZE=1 report) \
        done
	@for dir in $(NORMAL_PROBLEM_SIZE_DIRS); do \
	    (cd $$dir; \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -s -j1 TEST=valgrind \
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

# test target - Descend into projects/llvm-test and run the TEST.safecode.Makefile
# tests...
test::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=safecode \
                   $(LARGESIZE) $(STABLERUN) report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

pacompiletime::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=pacompiletime \
                   report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

ptrcomp::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=ptrcomp \
                   $(LARGESIZE) $(STABLERUN) report report.html)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

ptrcomptest::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=ptrcomp \
                   $(LARGESIZE) report)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

vtl::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=pavtl \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

perf::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=perf \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

optzn::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=optzn \
                   $(LARGESIZE) $(STABLERUN) test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

p4perf::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=p4perf \
                   test report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

strace::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=strace \
                   test)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

cputrack::
	(cd $(LLVM_OBJ_ROOT)/projects/llvm-test/$(SUBDIR); \
               PROJECT_DIR=$(PROJ_OBJ_ROOT) $(MAKE) -j1 TEST=cputrack \
                   report report.csv)
	@printf "\a"; sleep 1; printf "\a"; sleep 1; printf "\a"

