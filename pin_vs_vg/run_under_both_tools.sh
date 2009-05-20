#!/bin/bash
#
# Copyright 2009 Google Inc. All Rights Reserved.
# Author: timurrrr@google.com (Timur Iskhodzhanov)

COMMAND=$@
NUM_DIVISIONS=${NUM_DIVISIONS:-"5"}

if [ "$OUTPUT_PREFIX" == "" ]; then
  mkdir results 2>/dev/null
  OUTPUT_PREFIX=`date +%F-%H-%M-%S`
  OUTPUT_PREFIX="results/$OUTPUT_PREFIX"
fi

export TIMEFORMAT="VALGRIND TIME: %R"
time ./valgrind/inst/bin/valgrind --tool=testgrind --N=${NUM_DIVISIONS} --log-file="${OUTPUT_PREFIX}_valgrind.log" $@ 2>&1 | grep "TIME:"

export TIMEFORMAT="PIN TIME: %R"
time ./pin/pin -t pin_tool/pin_instrument_test.so -logfile "${OUTPUT_PREFIX}_pin.log" -N ${NUM_DIVISIONS} -- $@ 2>&1 | grep "TIME: "
