#!/bin/bash

set -e

BIN=${BIN:-"./build/inst/bin"}
TSAN=${TSAN:-"tsan"}
SUPP=${SUPP:-"--suppressions=`dirname $0`/tsan_apache.supp"}
SIZE=${SIZE:-10000}
echo $TSAN $SUPP ${BIN}/httpd -X -k start &
$TSAN $SUPP ${BIN}/httpd -X -k start &
sleep 5 # give it some time for initialization
${BIN}/ab -n $SIZE -c 50 http://localhost:8000/
${BIN}/httpd -X -k stop
