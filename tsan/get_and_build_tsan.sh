#!/bin/bash

TSAN_REV=1376

VALGRIND_INST_ROOT=${VALGRIND_INST_ROOT:-$HOME/tsan_inst}

# Get ThreadSanitizer. This will create directory 'tsan' and patch valgrind
svn co -r $TSAN_REV     http://data-race-test.googlecode.com/svn/trunk drt_trunk
cd drt_trunk

# Build Valgind.
(cd third_party && ./build_and_install_valgrind.sh)

# Build ThreadSanitizer.
(cd tsan && make l -j4 && make install)

# Build tests.
(cd unttest && make)

# Check if the ThreadSanitizer works:
$VALGRIND_INST_ROOT/bin/valgrind --tool=tsan --color unttest/racecheck_unittest 301
# You should now see the ThreadSanitizer's output.
# Done!
