#!/bin/bash
TSAN=${TSAN:="tsan"}
SUPP=${SUPP:-"--suppressions=`dirname $0`/tsan_apache.supp"}
echo $SUPP
$TSAN $SUPP build/inst/bin/httpd -X -k start &
sleep 5 # give it some time for initialization
build/inst/bin/ab -n 10000 -c 50 http://localhost:8000/
build/inst/bin/httpd -X -k stop
