LEVEL = ../..
BYTECODE_LIBRARY=1
SHARED_LIBRARY=1
LIBRARYNAME=poolalloc_bitmap

ifeq ($(OS),Linux)
CXX.Flags += -march=native
else
CXX.Flags += -march=nocona
endif

CXX.Flags += -fno-threadsafe-statics
include $(LEVEL)/Makefile.common

# Always build optimized and debug versions
all:: $(LIBNAME_OBJO) $(LIBNAME_OBJG)
