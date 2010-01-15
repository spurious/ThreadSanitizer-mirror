#!/bin/bash

# Where to install Valgrind with ThreadSanitizer.
VALGRIND_INST_ROOT="$1"
SVN_ROOT="$2"

if [ "$VALGRIND_INST_ROOT" == "" ]; then
  echo "Usage: $0 /tsan/installation/path [svn/root/dir]"
  exit
fi

if [ "$SVN_ROOT" == "" ]; then
# Get ThreadSanitizer. This will create directory 'drt'
  svn co http://data-race-test.googlecode.com/svn/trunk drt || exit 1
  cd drt || exit 1
else
  cd $SVN_ROOT || exit 1
fi

TOPDIR=`pwd`

ARCH=`uname -m`
OS=`uname -s`

echo ------------------------------------------------
echo Building ThreadSanitizer for $ARCH $OS
echo ------------------------------------------------
sleep 1

if [ "$ARCH $OS" == "x86_64 Linux" ]; then
  TARGET=lo  # 32- and 64-bit optimized linux binaries
elif [ "$ARCH $OS" == "i386 Linux" ]; then
  TARGET=l32o # 32-only optimized Linux binaries
elif [ "$ARCH $OS" == "i386 Darwin" ]; then
  TARGET=m32o # 32-only optimized Mac binaries
else
  echo "Unsupport platform: $ARCH $OS"
  exit 1
fi


# Build Valgind.
cd $TOPDIR/third_party || exit 1
./build_and_install_valgrind.sh $VALGRIND_INST_ROOT || exit 1

cd $TOPDIR/tsan || exit 1
make -s -j4 OFFLINE= GTEST_ROOT= PIN_ROOT= $TARGET || exit 1
make -s install VALGRIND_INST_ROOT=$VALGRIND_INST_ROOT  || exit 1

# Build the self contained binaries.
cd $TOPDIR || exit 1
tsan_binary/mk-self-contained-tsan.sh $VALGRIND_INST_ROOT tsan.sh  || exit 1

# Test
cd $TOPDIR/unittest || exit 1
make || exit 1
$TOPDIR/tsan.sh --color ./racecheck_unittest 301 || exit 1

# Done
echo "ThreadSanitizer is built: $TOPDIR/tsan.sh"
