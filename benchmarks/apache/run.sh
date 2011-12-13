#!/bin/bash

set -e

BIN=${BIN:-"./httpd-2.2.21/TSLL/bin"}
TSAN=${TSAN:-""}
SUPP=${SUPP:-""}
SIZE=${SIZE:-10000}
echo $TSAN $SUPP ${BIN}/httpd -X -k start &
$TSAN $SUPP ${BIN}/httpd -X -k start &
sleep 5 # give it some time for initialization
BUILD/CLANG/bin/ab -n $SIZE -c 50 http://localhost:8042/
${BIN}/httpd -X -k stop
