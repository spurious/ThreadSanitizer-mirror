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

strip() {
  grep -v "$1" test.out > test.out2
  mv -f test.out2 test.out
}

test_file() {
  SRC=$1
  COMPILER=$2
  echo ----- TESTING $1
  $COMPILER $SRC $CFLAGS -c -o tmp.o
  # Link with CXX, because lldb and suppressions require C++.
  $CXX tmp.o $LDFLAGS
  LD_LIBRARY_PATH=$LLDB ./a.out 2> test.out || echo -n
  if [ "$3" != "" ]; then
    cat test.out
  fi
  echo >>test.out  # FileCheck fails on empty files
  strip "failed to intercept"
  strip "Running under ThreadSanitizer"
  strip "========="
  FileCheck < test.out $SRC
  if [ "$3" == "" ]; then
    rm -f a.out test.out *.tmp *.tmp2
  fi
}

if [ "$1" == "" ]; then
  for c in $ROOTDIR/output_tests/*.c; do
    test_file $c $CC
  done
  for c in $ROOTDIR/output_tests/*.cc; do
    test_file $c $CXX
  done
else
  test_file $ROOTDIR/output_tests/$1 $CXX "DUMP"
fi
