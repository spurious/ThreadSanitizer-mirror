#!/bin/bash

set -x
set -e
set -u

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
CLANG_DIR=$SCRIPT_DIR/../../llvm
GCC_DIR=$SCRIPT_DIR/../../gcc
export PATH=$CLANG_DIR/bin:$GCC_DIR/bin:$PATH
export LD_LIBRARY_PATH=$GCC_DIR/lib64
export MAKEFLAGS=-j4
gcc -v 2>tmp && grep "version" tmp
clang -v 2>tmp && grep "version" tmp

if [ "$BUILDBOT_CLOBBER" != "" ]; then
  echo @@@BUILD_STEP clobber@@@
  rm -rf tsanv2
fi

echo @@@BUILD_STEP update@@@
REV_ARG=
if [ "$BUILDBOT_REVISION" != "" ]; then
  REV_ARG="-r$BUILDBOT_REVISION"
fi

if [ -d tsanv2 ]; then
  cd tsanv2
  svn up --ignore-externals $REV_ARG
  cd v2
else
  svn co http://data-race-test.googlecode.com/svn/trunk/ tsanv2 $REV_ARG
  cd tsanv2/v2
  make install_deps
fi

./scripts/slave/test.sh
