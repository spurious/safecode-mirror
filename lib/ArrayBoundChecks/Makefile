
LEVEL = ../../

BUILD_RELINKED=1

LIBRARYNAME=arrayboundcheck

SOURCES := ABCPreProcess.cpp AffineExpressions.cpp BottomUpCallGraph.cpp ArrayBoundCheck.cpp BreakConstantGEPs.cpp

CFLAGS   += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CPPFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CXXFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"

include $(LEVEL)/Makefile.common

