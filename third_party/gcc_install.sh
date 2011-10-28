#!/bin/bash

#check configuration parameters
if [ "$GCC_VER" == "" ]; then
  echo usage: GCC_VER=4.5.3 MAKEFLAGS=-j8 $0
  exit 1
fi

#install gcc prerequisites
sudo apt-get install flex bison libc6-dev libc6-dev-i386 libgmp3-dev libmpfr-dev libmpc-dev || exit 1

#setup dirs
GCC_ROOT=`pwd`/gcc-$GCC_VER
GCC_SRC=$GCC_ROOT/gcc-$GCC_VER
GCC_OBJ=$GCC_ROOT/build
GCC_INS=$GCC_ROOT/install
mkdir $GCC_ROOT
cd $GCC_ROOT || exit 1

#get gcc sources
wget ftp://ftp.fu-berlin.de/unix/languages/gcc/releases/gcc-$GCC_VER/gcc-$GCC_VER.tar.bz2 || exit 1
tar -jxvf gcc-$GCC_VER.tar.bz2 || exit 1

#configure and build gcc
mkdir $GCC_OBJ
cd $GCC_OBJ || exit 1
$GCC_SRC/configure --enable-languages=c,c++ --disable-bootstrap --enable-checking=no --with-gnu-as --with-gnu-ld --with-ld=/usr/bin/ld.bfd --prefix=$GCC_INS || exit 1
make || exit 1
make install || exit 1

