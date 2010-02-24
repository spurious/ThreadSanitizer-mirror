#!/bin/bash

VALGRIND_REV=11055
VEX_REV=1961

do_checkout_and_cd() {
  echo "No directory 'valgrind'; doing svn checkout"
  svn co -r $VALGRIND_REV svn://svn.valgrind.org/valgrind/trunk valgrind
  cd valgrind || exit 1
}

cd valgrind || do_checkout_and_cd

svn up -r $VALGRIND_REV
svn up -r $VEX_REV VEX/

for p in ../../valgrind_patches/*.patch; do
  echo ==================== applying $p =================
  patch -p 0 -N < $p || true
done
