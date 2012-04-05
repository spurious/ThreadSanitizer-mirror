#!/bin/bash

set -x
set -e
set -u

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
CLANG_DIR=$SCRIPT_DIR/../../llvm
GCC_DIR=$SCRIPT_DIR/../../gcc
export PATH=$CLANG_DIR/bin:$GCC_DIR/bin:$PATH
export LD_LIBRARY_PATH=$GCC_DIR/lib64
export MAKEFLAGS=-j4
clang -v
gcc -v

if [ "$BUILDBOT_CLOBBER" != "" ]; then
  echo @@@BUILD_STEP clobber@@@
  rm -rf v2
fi

echo @@@BUILD_STEP update@@@
REV_ARG=
if [ "$BUILDBOT_REVISION" != "" ]; then
  REV_ARG="-r$BUILDBOT_REVISION"
fi

MAKE_JOBS=${MAX_MAKE_JOBS:-16}

if [ -d tsanv2 ]; then
  cd tsanv2/v2
  svn up $REV_ARG
else
  svn co http://data-race-test.googlecode.com/svn/trunk/ tsanv2 $REV_ARG
  cd tsanv2/v2
  make install_deps
fi

echo @@@BUILD_STEP LINT@@@
make lint

echo @@@BUILD_STEP BUILD DEBUG-CLANG@@@
make clean
make DEBUG=1 CC=clang CXX=clang++

echo @@@BUILD_STEP TEST DEBUG-CLANG@@@
./tests/tsan_test

echo @@@BUILD_STEP BUILD RELEASE-GCC@@@
make clean
make DEBUG=0 CC=gcc CXX=g++

echo @@@BUILD_STEP TEST RELEASE-GCC@@@
./tests/tsan_test

echo @@@BUILD_STEP OUTPUT TESTS@@@
(cd output_tests && ./test_output.sh)

echo
echo @@@BUILD_STEP RACECHECK UNITTEST@@@
(cd ../unittest && \
rm -f bin/racecheck_unittest-linux-amd64-O0 && \
OMIT_DYNAMIC_ANNOTATIONS_IMPL=1 LIBS=../v2/tsan/libtsan.a make l64 -j16 CC=clang CXX=clang++ LDOPT="-pie -ldl ../v2/tsan/libtsan.a" OMIT_CPP0X=1 EXTRA_CFLAGS="-fthread-sanitizer -fPIC -g -O2 -Wno-format-security -Wno-null-dereference -Wno-format-security -Wno-null-dereference" EXTRA_CXXFLAGS="-fthread-sanitizer -fPIC -g -O2 -Wno-format-security -Wno-null-dereference -Wno-format-security -Wno-null-dereference" && \
bin/racecheck_unittest-linux-amd64-O0 --gtest_filter=-*Ignore*:*Suppress*:*EnableRaceDetectionTest*:*Rep*Test*:*NotPhb*:*Barrier*:*Death*:*PositiveTests_RaceInSignal*:StressTests.FlushStateTest)
#Ignore: ignores do not work yet
#Suppress: suppressions do not work yet
#EnableRaceDetectionTest: the annotation is not supported
#Rep*Test: uses inline assembly
#NotPhb: not-phb is not supported
#Barrier: pthread_barrier_t is not fully supported yet
#Death: there is some flakyness
#PositiveTests_RaceInSignal: signal() is not intercepted yet
#StressTests.FlushStateTest: uses suppressions

