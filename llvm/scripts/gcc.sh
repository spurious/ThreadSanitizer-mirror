#!/bin/bash
#
# A wrapper for g++ that detects whether g++ is used as a compiler or linker
# and either instruments the compiled source files or calls ld.sh to link
# the object files.

set -x
HERE=`dirname $0`

LLVM_PATH="$HERE"/../llvm/inst/bin
PASS_SO="$HERE"/../llvm/inst/lib/libLLVMMops.so
LLVM_GCC="$HERE"/../llvm-gcc-4.2/inst/bin/llvm-g++
OPT=$LLVM_PATH/opt
LLC=$LLVM_PATH/llc
MOPS_OBJ=mop_impl/mop_impl32.o

ALL_ARGS=
ARGS=
LD_MODE=


until [ -z "$1" ]
do
  ALL_ARGS="$ALL_ARGS $1"
  if [ `expr match "$1" ".*\.cc"` -gt 0 ]
  then
    SRC="$1"
  elif [ `expr match "$1" "-o"` -gt 0 ]
  then
    if [ "$1" == "-o" ]
    then
      shift
      ALL_ARGS="$ALL_ARGS $1"
      SRC_OBJ="$1"
    else
      SRC_OBJ=${1:2}
    fi
  elif [ `expr match "$1" ".*\.[ao]"` -gt 0 ]
  then
    LD_MODE=1
  elif [ `expr match "$1" "-c\|-O\|-std=\|-Werror"` -gt 0 ]
  then
    # pass
    echo "Dropped arg: $1"
  else
    ARGS="$ARGS $1"
  fi
  shift
done

if [ "$LD_MODE" == "1" ]
then
  $HERE/ld.sh $ALL_ARGS
  exit
fi


#SRC=$1
echo $ARGS
FNAME=`echo $SRC | sed 's/\.[^.]*$//'`
SRC_BIT="$FNAME.ll"
SRC_INSTR="$FNAME-instr.ll"
SRC_ASM="$FNAME.S"
if [ -z $SRC_OBJ ]
then
  SRC_OBJ=`basename $FNAME.o`
fi
SRC_EXE="$FNAME"
SRC_DBG="$SRC_OBJ.dbg"

INST_MODE=-offline
INST_MODE=-online
CXXFLAGS=
LDFLAGS="-lpthread -Wl,--wrap,pthread_create "
LDFLAGS+="-Wl,--wrap,pthread_mutex_lock -Wl,--wrap,pthread_mutex_unlock"

LOG=instrumentation.log

#exit
# Translate C code to LLVM bitcode.
$LLVM_GCC -emit-llvm -m32 $SRC -g -S $CXXFLAGS $ARGS -o "$SRC_BIT" || exit 1
# Instrument the bitcode.
$OPT -load "$PASS_SO" $INST_MODE "$SRC_BIT" -S  > "$SRC_INSTR" 2>$LOG || exit 1
cat $LOG | grep "^->" | sed "s/^->//" > "$SRC_DBG"
cat $LOG | grep -v "^->"
# Translate LLVM bitcode to native assembly code.
$LLC -march=x86 -O0 $SRC_INSTR  -o $SRC_ASM || exit 1
# Compile the object file.
$LLVM_GCC -m32 -c $SRC_ASM -O0 -g -o $SRC_OBJ

