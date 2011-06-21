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

arith.h: arithchk.c
	$(CC) arithchk.c || $(CC) -DNO_LONG_LONG arithchk.c
	./a.out >arith.h
	rm -f a.out

gd_qnan.h: arith.h qnan.c
	$(CC) qnan.c
	./a.out >gd_qnan.h
	rm -f a.out
