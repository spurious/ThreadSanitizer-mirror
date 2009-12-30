#!/bin/bash
# Valgrind sources reside in third_party/valgrind.
# To be safe, we sync to a specific version which is known to work.

VALGRIND_REV=10974
VEX_REV=1946

VALGRIND_INST_ROOT="$1"

mkdir -p $VALGRIND_INST_ROOT || exit 1

cd valgrind
svn up -r $VALGRIND_REV
svn up -r $VEX_REV      VEX/
make distclean
./autogen.sh && \
  ./configure --prefix=$VALGRIND_INST_ROOT && \
  make -s -j 8 && \
  make -s install
