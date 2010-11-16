#!/bin/bash
#

PASS_SO="$SCRIPT_ROOT/../opt/ThreadSanitizer/ThreadSanitizer.so"

if [ -z "$TSAN_AR" ]
then
  TSAN_AR=/usr/bin/ar
fi

# Note that we're using g++ instead of ld
if [ -z "$TSAN_LD" ]
then
  TSAN_LD=/usr/bin/g++
fi

LLVM_GCC=llvm-gcc
LLVM_GPP=llvm-g++
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
