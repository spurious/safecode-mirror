LEVEL = ..

PARALLEL_DIRS = ArrayBoundChecks \
                ConvertUnsafeAllocas \
                InsertPoolChecks \
                OptimizeChecks \
                StackSafety \
                PointerChecks \
                SpeculativeChecking \
                RewriteOOB \
                FaultInjector \
                IndirectCallChecks \
                DebugInstrumentation \

include $(LEVEL)/Makefile.common

ifneq ($(OS),Darwin)
OPTIONAL_DIRS := Safecode
endif

