#!/bin/bash
cd clang_src
ROOT=`pwd`/..
patch -p 0 < $ROOT/llvm/scripts/clang/clang.patch
ln -s $ROOT/llvm/opt/ThreadSanitizer/ThreadSanitizer.h lib/Transforms/Instrumentation/ThreadSanitizer.h
ln -s $ROOT/llvm/opt/ThreadSanitizer/ThreadSanitizer.cpp lib/Transforms/Instrumentation/ThreadSanitizer.cpp
ln -s $ROOT/llvm/opt/ThreadSanitizer/ThreadSanitizer.exports lib/Transforms/Instrumentation/ThreadSanitizer.exports

