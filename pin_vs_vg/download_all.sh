#!/bin/bash

# Copyright 2009 Google Inc. All Rights Reserved.
# Author: timurrrr@google.com (Timur Iskhodzhanov)

# Get PIN
wget http://www.pintool.org/cgi-bin/download.cgi?file=pin-2.6-24110-gcc.4.0.0-ia32_intel64-linux.tar.gz
tar xvfz pin-2.6-24110-gcc.4.0.0-ia32_intel64-linux.tar.gz
mv pin-2.5-24110-gcc.4.0.0-ia32_intel64-linux pin # note the wrong name of the
                                                  # extracted directory :-)

# Get Valgrind
svn co svn://svn.valgrind.org/valgrind/tags/VALGRIND_3_4_1 valgrind

# Add testgrind
mkdir valgrind/testgrind
mkdir valgrind/testgrind/docs
touch valgrind/testgrind/docs/Makefile.am
mkdir valgrind/testgrind/tests
touch valgrind/testgrind/tests/Makefile.am
cd valgrind/testgrind
ln -s ../../valgrind_tool/[A-Za-z]* .
ln -s ../../common/drd_benchmark_simple.h .
cd ..
patch -p0 <../valgrind.patch
cd ..

# Build valgrind
cd valgrind
./autogen.sh && ./configure --prefix=`pwd`/inst && make -j 8 install
