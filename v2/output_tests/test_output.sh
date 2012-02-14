#!/bin/bash

set -e # fail on any error

# Assuming clang is in path.
CC=clang
CXX=clang++

CCFLAGS="-fthread-sanitizer -fPIE"
LDFLAGS="-pie -lpthread -ldl -lstdc++ ../tsan/libtsan.a"

test_file() {
  COMPILER=$2
  SRC=$1
  $COMPILER $SRC $CCFLAGS $LDFLAGS
  ./a.out 2> test.out
  FileCheck < test.out $SRC
  cat test.out
  rm -f a.out test.out *.tmp *.tmp2
}


for c in *.c; do
  test_file $c $CC
done
