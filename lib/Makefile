LEVEL = ..

PARALLEL_DIRS = Support \
                ArrayBoundChecks \
                ConvertUnsafeAllocas \
                CStdLib \
                InsertPoolChecks \
                OptimizeChecks \
                StackSafety \
                SpeculativeChecking \
                RewriteOOB \
                FaultInjector \
                IndirectCallChecks \
                DebugInstrumentation \
                CStdLib \
#                PointerChecks \

include $(LEVEL)/Makefile.common

ifneq ($(OS),Darwin)
OPTIONAL_DIRS := Safecode
endif

