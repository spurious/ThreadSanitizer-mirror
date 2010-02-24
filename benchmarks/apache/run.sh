#!/bin/bash
TSAN=${TSAN:-"tsan"}
SUPP=${SUPP:-"--suppressions=`dirname $0`/tsan_apache.supp"}
SIZE=${SIZE:-10000}
echo $TSAN $SUPP build/inst/bin/httpd -X -k start &
$TSAN $SUPP build/inst/bin/httpd -X -k start &
sleep 5 # give it some time for initialization
build/inst/bin/ab -n $SIZE -c 50 http://localhost:8000/
build/inst/bin/httpd -X -k stop
