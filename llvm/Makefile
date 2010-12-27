# Copyright 2010 Google Inc. All Rights Reserved.
# Author: glider@google.com (Alexander Potapenko)

# Just The Makefile for LLVM instrumentation subsystem.

OPT_PATH=./opt/ThreadSanitizer
RTL_PATH=./tsan_rtl
OPT=$(OPT_PATH)/ThreadSanitizer.so
RTL_32_A=$(RTL_PATH)/tsan_rtl32.a
RTL_64_A=$(RTL_PATH)/tsan_rtl64.a
TARGETS=$(OPT) $(RTL_32_A) $(RTL_64_A)

all:	
	make -C $(OPT_PATH)
	make -C $(RTL_PATH)
	make -C $(RTL_PATH)
