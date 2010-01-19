#!/bin/bash
DYNAMORIO_ROOT=${DYNAMORIO_ROOT:=$HOME/DynamoRIO}

TS_ROOT=${TS_ROOT:-.}
TS_VARIANT=-debug

TS_FLAGS=

$DYNAMORIO_ROOT/bin32/drdeploy  -client \
   $TS_ROOT/bin/x86-linux-debug-ts_dynamorio.so 0 "$TS_FLAGS" \
   "$@"
