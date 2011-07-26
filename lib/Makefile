LEVEL = ..

PARALLEL_DIRS = \
               Support \
               InsertPoolChecks \
               OptimizeChecks \
               RewriteOOB \
               DebugInstrumentation \
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

