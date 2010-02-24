#!/bin/bash

source valgrind_rev.sh

VALGRIND_PACK="$1"
if [ "$VALGRIND_PACK" == "" ]; then
  VALGRIND_PACK=valgrind-r${VALGRIND_REV}-vex-r${VEX_REV}.tar.bz2
fi

update_subversion() {
  svn up -r $VALGRIND_REV
  svn up -r $VEX_REV VEX/
}

unpack() {
  rm -rf valgrind
  tar xjvf ${VALGRIND_PACK}
}

checkout() {
  echo "No directory 'valgrind'; doing svn checkout"
  svn co -r $VALGRIND_REV svn://svn.valgrind.org/valgrind/trunk valgrind
}

if [[ -e $VALGRIND_PACK ]]; then
  unpack
  cd valgrind || exit 1
elif [[ -d valgrind ]]; then
  cd valgrind && update_subversion
else
  checkout && cd valgrind && update_subversion
fi

for p in ../../valgrind_patches/*.patch; do
  echo ==================== applying $p =================
  patch -p 0 -N < $p || true
done
