#!/bin/bash

# Where to install Valgrind with ThreadSanitizer.
VALGRIND_INST_ROOT=${VALGRIND_INST_ROOT:-$HOME/tsan_inst}

# Get ThreadSanitizer. This will create directory 'drt_trunk'
svn co http://data-race-test.googlecode.com/svn/trunk drt_trunk
cd drt_trunk

# Build Valgind.
(cd third_party && ./build_and_install_valgrind.sh)

# Build ThreadSanitizer.
(cd tsan && make -s -j4 OFFLINE= GTEST_ROOT= PIN_ROOT= l && make -s install VALGRIND_INST_ROOT=$VALGRIND_INST_ROOT)

# Build tests.
(cd unittest && make)

# Check if the ThreadSanitizer works:
$VALGRIND_INST_ROOT/bin/valgrind --tool=tsan --color unittest/racecheck_unittest 301
# You should now see the ThreadSanitizer's output.
# Done!
