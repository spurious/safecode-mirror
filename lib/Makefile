LEVEL = ..

PARALLEL_DIRS = \
               InsertPoolChecks \
               OptimizeChecks \
               RewriteOOB \
               DebugInstrumentation \
#               Support \
#               ArrayBoundChecks \
#               BaggyBoundsChecks \
#               ConvertUnsafeAllocas \
#               CStdLib \
#               FormatStrings \
#               StackSafety \
#               SpeculativeChecking \
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

