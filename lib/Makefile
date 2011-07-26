LEVEL = ..

PARALLEL_DIRS = \
#               Support \
#               ArrayBoundChecks \
#               BaggyBoundsChecks \
#               ConvertUnsafeAllocas \
#               CStdLib \
#               FormatStrings \
#               InsertPoolChecks \
#               OptimizeChecks \
#               StackSafety \
#               SpeculativeChecking \
#               RewriteOOB \
#               DebugInstrumentation \
#               DanglingPointers \
#               CStdLib \
#               Utility \
#               FaultInjector \
#               IndirectCallChecks \
#               PointerChecks \

include $(LEVEL)/Makefile.common

ifneq ($(OS),Darwin)
OPTIONAL_DIRS := Safecode
endif

