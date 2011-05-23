#!/bin/bash

VG_REPO=http://valgrind-variant.googlecode.com/svn/trunk/valgrind
VG_REV=90
VG_DIR=$(dirname "${0}")
VG_DIR=$(cd "${VG_DIR}" && /bin/pwd)/valgrind

update_subversion() {
  svn up -r${VG_REV}
}

checkout() {
  echo "No directory '${VG_DIR}'; doing svn checkout"
  svn co -r ${VG_REV} ${VG_REPO} ${VG_DIR}
}

if [[ -d ${VG_DIR} ]]; then
  cd ${VG_DIR} && update_subversion
else
  checkout
fi

