LEVEL = ../../

LIBRARYNAME=scutility

BUILD_ARCHIVE := 1

#
# Build shared libraries on all platforms except Cygwin and MingW (which do
# not support them).
#
ifneq ($(OS),Cygwin)
ifneq ($(OS),MingW)
#SHARED_LIBRARY := 1
endif
endif

SOURCES := SCPoolHeuristic.cpp

include $(LEVEL)/Makefile.common

