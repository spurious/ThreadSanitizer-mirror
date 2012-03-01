#!/bin/bash

set -x
set -e
set -u

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

if [ -d v2 ]; then
  cd v2
  svn up $REV_ARG
else
  svn co http://data-race-test.googlecode.com/svn/trunk/v2 v2 $REV_ARG
  cd v2
  make get_third_party
  make get_interception
fi

echo @@@BUILD_STEP LINT@@@
make lint

echo @@@BUILD_STEP BUILD DEBUG-GCC@@@
make clean
make DEBUG=1 CC=gcc CXX=g++

echo @@@BUILD_STEP TEST DEBUG-GCC@@@
./tests/tsan_test
./tests/tsan_c_test

echo @@@BUILD_STEP BUILD RELEASE-CLANG@@@
make clean
CLANG_DIR=../../../../../clang
CFLAGS=-Wno-null-dereference LD_LIBRARY_PATH=$CLANG_DIR/bin make CC=$CLANG_DIR/bin/clang CXX=$CLANG_DIR/bin/clang++

echo @@@BUILD_STEP TEST RELEASE-CLANG@@@
./tests/tsan_test
./tests/tsan_c_test

echo @@@BUILD_STEP OUTPUT TESTS@@@
cd output_tests
PATH=$CLANG_DIR/bin:$PATH ./test_output.sh
