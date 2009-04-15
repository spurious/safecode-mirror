LEVEL = ..

PARALLEL_DIRS = ArrayBoundChecks \
                ConvertUnsafeAllocas \
                CStdLib \
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

