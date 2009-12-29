#!/bin/bash
# Valgrind sources reside in third_party/valgrind.
# To be safe, we make sync to a specific version which is known to work.

VALGRIND_REV=10886
VEX_REV=1946

VALGRIND_INST_ROOT=${VALGRIND_INST_ROOT:-$HOME/drt/vg}

cd valgrind
svn up -r $VALGRIND_REV
svn up -r $VEX_REV      VEX/
make distclean
./autogen.sh && \
  ./configure --prefix=`pwd`/inst && \
  make -j 8 && \
  make install
