LEVEL = ..

PARALLEL_DIRS = ArrayBoundChecks \
                ConvertUnsafeAllocas \
                InsertPoolChecks \
                StackSafety \
                PointerChecks \
                SpeculativeChecking \
                FaultInjector \
                IndirectCallChecks \
                DebugInstrumentation

include $(LEVEL)/Makefile.common

ifneq ($(OS),Darwin)
OPTIONAL_DIRS := Safecode
endif

