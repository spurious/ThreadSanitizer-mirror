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
  rm -rf compiler-rt
fi

echo @@@BUILD_STEP update@@@
REV_ARG=
if [ "$BUILDBOT_REVISION" != "" ]; then
  REV_ARG="-r$BUILDBOT_REVISION"
fi

if [ -d compiler-rt ]; then
  (cd tsanv2 && svn up --ignore-externals)
  (cd compiler-rt && svn up $REV_ARG)
else
  svn co http://data-race-test.googlecode.com/svn/trunk/ tsanv2
  svn co $REV_ARG http://llvm.org/svn/llvm-project/compiler-rt/trunk/ compiler-rt
  (cd compiler-rt/lib/tsan && make -f Makefile.old install_deps)
fi

cp tsanv2/v2/scripts/slave/test.sh compiler-rt/lib/tsan
(cd compiler-rt/lib/tsan && ./test.sh)
