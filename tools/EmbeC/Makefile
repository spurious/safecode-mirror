LEVEL = ../..
TOOLNAME = embec
LLVMLIBS = bcreader bcwriter instrument \
           profpaths scalaropts \
           ipo ipa.a datastructure.a transforms target.a analysis \
	   transformutils vmcore support cwriter
USEDLIBS = pointerchecks arrayboundcheck
TOOLLINKOPTS = -ldl 
include $(LEVEL)/Makefile.common
