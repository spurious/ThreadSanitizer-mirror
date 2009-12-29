#!/bin/bash
# Valgrind sources reside in third_party/valgrind.
# To be safe, we sync to a specific version which is known to work.

VALGRIND_REV=10971
VEX_REV=1946

VALGRIND_INST_ROOT=${VALGRIND_INST_ROOT:-$HOME/tsan_inst}

cd valgrind
svn up -r $VALGRIND_REV
svn up -r $VEX_REV      VEX/
make distclean
./autogen.sh && \
  ./configure --prefix=$VALGRIND_INST_ROOT && \
  make -j 8 && \
  make install
