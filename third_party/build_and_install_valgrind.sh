#!/bin/bash
# Valgrind sources reside in third_party/valgrind.
# To be safe, we sync to a specific version which is known to work.

VALGRIND_INST_ROOT="$1"

if [ "$VALGRIND_INST_ROOT" == "" ]; then
  echo "Usage: $0 /tsan/installation/path"
  exit
fi

./update_valgrind.sh
cd valgrind

for p in ../../valgrind_patches/*.patch; do
  echo ==================== applying $p =================
  patch -p 0 < $p
done

make distclean
./autogen.sh && \
  ./configure --prefix=$VALGRIND_INST_ROOT && \
  make -s -j 8 && \
  make -s install
