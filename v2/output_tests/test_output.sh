#!/bin/bash

ulimit -s 8192;
set -e # fail on any error

ROOTDIR=`dirname $0`/..

# Assuming clang is in path.
CC=clang
CXX=clang++

# TODO: add testing for all of -O0...-O3
CFLAGS="-fthread-sanitizer -fPIE -O1 -g"
LDFLAGS="-pie -lpthread -ldl $ROOTDIR/tsan/libtsan.a"
if [ "$LLDB" != "" ]; then
  LDFLAGS+=" -L$LLDB -llldb"
fi

test_file() {
  SRC=$1
  COMPILER=$2
  echo ----- TESTING $1
  $COMPILER $SRC $CFLAGS $LDFLAGS
  LD_LIBRARY_PATH=$LLDB ./a.out 2> test.out || echo -n
  echo >>test.out  # FileCheck fails on empty files
  # cat test.out
  FileCheck < test.out $SRC
  rm -f a.out test.out *.tmp *.tmp2
  # echo
}

for c in $ROOTDIR/output_tests/*.c; do
  test_file $c $CC
done

for c in $ROOTDIR/output_tests/*.cc; do
  test_file $c $CXX
done
