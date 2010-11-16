#!/bin/bash
#

PASS_SO="$SCRIPT_ROOT/../opt/ThreadSanitizer/ThreadSanitizer.so"
LLVM_GCC=llvm-gcc
LLVM_GPP=llvm-g++
LD=g++
OPT=opt
LLC=llc
LINK_DBG="$SCRIPT_ROOT/link_debuginfo.sh"
TSAN_RTL="$SCRIPT_ROOT/../tsan_rtl/tsan_rtl32.a"

PLATFORM="x86"
set_platform_dependent_vars () {
  if [ "$PLATFORM" == "x86-64" ]
  then
    MARCH="-m64"
    XARCH="x86-64"
    TSAN_RTL="$SCRIPT_ROOT/../tsan_rtl/tsan_rtl64.a"
  else
    MARCH="-m32"
    XARCH="x86"
    TSAN_RTL="$SCRIPT_ROOT/../tsan_rtl/tsan_rtl32.a"
  fi
}
