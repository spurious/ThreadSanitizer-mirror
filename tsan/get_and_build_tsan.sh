#!/bin/bash

# Where to install Valgrind with ThreadSanitizer.
VALGRIND_INST_ROOT="$1"

if [ "$VALGRIND_INST_ROOT" == "" ]; then
  echo "Usage: $0 /tsan/installation/path"
  exit
fi

# Get ThreadSanitizer. This will create directory 'drt'
svn co http://data-race-test.googlecode.com/svn/trunk drt || exit 1
cd drt || exit 1

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
tsan_binary/mk-self-contained-tsan.sh $VALGRIND_INST_ROOT tsan  || exit 1

# Test
cd $TOPDIR/unittest || exit 1
make || exit 1
$TOPDIR/tsan --color ./racecheck_unittest 301 || exit 1

# Done
echo "ThreadSanitizer is built: $TOPDIR/tsan"
