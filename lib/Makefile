LEVEL = ..

PARALLEL_DIRS = \
               Support \
               ArrayBoundChecks \
               InsertPoolChecks \
               ConvertUnsafeAllocas \
               OptimizeChecks \
               RewriteOOB \
               DebugInstrumentation \
#               BaggyBoundsChecks \
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

