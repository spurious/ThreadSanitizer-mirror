##===- lib/Transforms/Hello/Makefile -----------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

#LEVEL = ../../..
#LIBRARYNAME = LLVMThreadSanitizer
#LOADABLE_MODULE = 1
#USEDLIBS =
#
## If we don't need RTTI or EH, there's no reason to export anything
## from the hello plugin.
#ifneq ($(REQUIRES_RTTI), 1)
#ifneq ($(REQUIRES_EH), 1)
#EXPORTED_SYMBOL_FILE = $(PROJ_SRC_DIR)/ThreadSanitizer.exports
#endif
#endif
#
#include $(LEVEL)/Makefile.common
#

CXX=g++

ThreadSanitizer.so:	ThreadSanitizer.cpp
	$(CXX) -c -O0 -fPIC -g ThreadSanitizer.cpp -o ThreadSanitizer.o -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS
	$(CXX) -shared -g ThreadSanitizer.o -o ThreadSanitizer.so

clean:
	rm -f *.o
	rm -f *.so
