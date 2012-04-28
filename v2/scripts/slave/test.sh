#!/bin/bash

set -x
set -e
set -u

echo @@@BUILD_STEP LINT@@@
make lint

echo @@@BUILD_STEP BUILD DEBUG-CLANG@@@
make clean
make DEBUG=1 CC=clang CXX=clang++

echo @@@BUILD_STEP TEST DEBUG-CLANG@@@
./tests/tsan_test

echo @@@BUILD_STEP BUILD STATS/OUTPUT@@@
make clean
make DEBUG=1 CC=clang CXX=clang++ CFLAGS="-DTSAN_COLLECT_STATS=1 -DTSAN_DEBUG_OUTPUT=2"

echo @@@BUILD_STEP BUILD SHADOW_COUNT=4@@@
make clean
make DEBUG=1 CC=clang CXX=clang++ CFLAGS=-DTSAN_SHADOW_COUNT=4

echo @@@BUILD_STEP TEST SHADOW_COUNT=4@@@
./tests/tsan_test

echo @@@BUILD_STEP BUILD SHADOW_COUNT=2@@@
make clean
make DEBUG=1 CC=clang CXX=clang++ CFLAGS=-DTSAN_SHADOW_COUNT=2

echo @@@BUILD_STEP TEST SHADOW_COUNT=2@@@
./tests/tsan_test

echo @@@BUILD_STEP BUILD RELEASE-GCC@@@
make clean
make DEBUG=0 CC=gcc CXX=g++

echo @@@BUILD_STEP TEST RELEASE-GCC@@@
./tests/tsan_test

echo @@@BUILD_STEP OUTPUT TESTS@@@
(cd output_tests && ./test_output.sh)

echo @@@BUILD_STEP ANALYZE@@@
./check_analyze.sh

echo @@@BUILD_STEP RACECHECK UNITTEST@@@
(cd ../unittest && \
rm -f bin/racecheck_unittest-linux-amd64-O0 && \
OMIT_DYNAMIC_ANNOTATIONS_IMPL=1 LIBS=../v2/tsan/libtsan.a make l64 -j16 CC=clang CXX=clang++ LDOPT="-pie -ldl ../v2/tsan/libtsan.a" OMIT_CPP0X=1 EXTRA_CFLAGS="-fthread-sanitizer -fPIC -g -O2 -Wno-format-security -Wno-null-dereference -Wno-format-security -Wno-null-dereference" EXTRA_CXXFLAGS="-fthread-sanitizer -fPIC -g -O2 -Wno-format-security -Wno-null-dereference -Wno-format-security -Wno-null-dereference" && \
bin/racecheck_unittest-linux-amd64-O0 --gtest_filter=-*Ignore*:*Suppress*:*EnableRaceDetectionTest*:*Rep*Test*:*NotPhb*:*Barrier*:*Death*:*PositiveTests_RaceInSignal*:StressTests.FlushStateTest:*Mmap84GTest )

#Ignore: ignores do not work yet
#Suppress: suppressions do not work yet
#EnableRaceDetectionTest: the annotation is not supported
#Rep*Test: uses inline assembly
#NotPhb: not-phb is not supported
#Barrier: pthread_barrier_t is not fully supported yet
#Death: there is some flakyness
#PositiveTests_RaceInSignal: signal() is not intercepted yet
#StressTests.FlushStateTest: uses suppressions
#Mmap84GTest: too slow, causes paging

