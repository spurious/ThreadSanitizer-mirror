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

TSAN_PATH=../../../tsan
OPT=-O2
FLAGS=-DBUILD_TSAN_FOR_OLD_LLVM -g

ThreadSanitizer.so:	ThreadSanitizer.cpp ThreadSanitizer.h ignore.o common_util.o
	$(CXX) -c $(OPT) -I$(TSAN_PATH) $(FLAGS) -fPIC -g ThreadSanitizer.cpp -o ThreadSanitizer.o -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS
	$(CXX) -shared -g ThreadSanitizer.o ignore.o common_util.o -o ThreadSanitizer.so

ignore.o:	$(TSAN_PATH)/ignore.cc $(TSAN_PATH)/ignore.h
	$(CXX) -c $(OPT) -fPIC -g $< -o $@

common_util.o:	$(TSAN_PATH)/common_util.cc $(TSAN_PATH)/common_util.h
	$(CXX) -c $(OPT) -fPIC -g $< -o $@

clean:
	rm -f *.o
	rm -f *.so
