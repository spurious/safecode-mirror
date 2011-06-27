##===- lib/FloatConversion/Makefile -----------------*- Makefile -*-===##

#
# Indicate where we are relative to the top of the source tree.
#
LEVEL=../..

LIBRARYNAME=gdtoa

BYTECODE_LIBRARY = 1

BUILT_SOURCES = arith.h gd_qnan.h

SOURCES = dmisc.c dtoa.c gdtoa.c gmisc.c hdtoa.c ldtoa.c misc.c

#
# Include Makefile.common so we know what to do.
#
include $(LEVEL)/Makefile.common

SRCPATH=$(PROJ_SRC_ROOT)/runtime/FloatConversion

arith.h: $(SRCPATH)/arithchk.c
	$(CC) $(SRCPATH)/arithchk.c -o arithchk
	./arithchk >$(SRCPATH)/arith.h
	rm -f arithchk

gd_qnan.h: $(SRCPATH)/arith.h $(SRCPATH)/qnan.c
	$(CC) $(SRCPATH)/qnan.c -o gd_qnan
	./gd_qnan >$(SRCPATH)/gd_qnan.h
	rm -f gd_qnan
