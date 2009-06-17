
LEVEL = ../../

LIBRARYNAME=arrayboundcheck

SOURCES := ABCPreProcess.cpp AffineExpressions.cpp BottomUpCallGraph.cpp \
ArrayBoundCheck.cpp BreakConstantGEPs.cpp ArrayBoundCheckDummy.cpp \
ArrayBoundCheckLocal.cpp

CFLAGS   += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CPPFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CXXFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"

include $(LEVEL)/Makefile.common

