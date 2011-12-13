#!/bin/bash
# Call this script in a directory where you have downloaded and
# extracted apache (see get.sh).
# Configure apache with mpm=worker so that it runs in multiple threads.
# Also, define USE_ATOMICS_GENERIC so that pthread mutexes are used.
set -e
set -x
mkdir build && cd build
CFLAGS="-O0 -g -DUSE_ATOMICS_GENERIC=1 " \
  ../httpd-2.2.21/configure  --prefix=`pwd`/inst --with-mpm=worker
make -j && make install

# set a port that does not require you to be root.
sed -i 's/Listen 80/Listen 8000/g' inst/conf/httpd.conf

# some magic configs
cat << EOF >> inst/conf/httpd.conf
<IfModule worker.c>
ListenBackLog     50000
StartServers         2
ThreadLimit        500
ThreadsPerChild    20
MinSpareThreads    20
MaxSpareThreads    20
ThreadsPerChild    20
MaxClients       320
MaxRequestsPerChild  0
</IfModule>
EOF
