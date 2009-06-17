LEVEL = ..

PARALLEL_DIRS = Support \
                ArrayBoundChecks \
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
                CStdLib \

include $(LEVEL)/Makefile.common

ifneq ($(OS),Darwin)
OPTIONAL_DIRS := Safecode
endif

