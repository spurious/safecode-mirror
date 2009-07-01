
LEVEL = ../../

LIBRARYNAME=arrayboundcheck

SOURCES := AffineExpressions.cpp BottomUpCallGraph.cpp \
BreakConstantGEPs.cpp ArrayBoundCheckDummy.cpp \
ArrayBoundCheckLocal.cpp

#ABCPreProcess.cpp ArrayBoundCheck.cpp 

CFLAGS   += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CPPFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"
CXXFLAGS += -DOMEGASCRIPT=\"${PROJ_SRC_ROOT}/utils/omega.pl\"

include $(LEVEL)/Makefile.common

