#!/bin/bash
#
# Copyright 2010 Google Inc. All Rights Reserved.
# Author: glider@google.com (Alexander Potapenko)

SCRIPT_ROOT=`dirname $0`
source "$SCRIPT_ROOT/common.sh"
ALL_ARGS=
ARGS=
LD_MODE=
# Optimization level: -O0, -O1 etc.
OX=-O0
OPT_OX=
# -fPIC flag.
FPIC=
# llc analog of -fPIC is -relocation-model=pic
LLC_PIC=
DEBUG=
# If true, we're compiling an assembly file.
FROM_ASM=
OPT_PASSES=-adce
OPT_PASSES="-reg2mem -mem2reg -adce"
OPT_PASSES=-verify
OPT_PASSES=
COMPILE_FAST=1
#COMPILE_FAST=

until [ -z "$1" ]
do
  ALL_ARGS="$ALL_ARGS $1"
  if [ `expr match "$1" ".*\.cc"` -gt 0 ]
  then
    SRC="$1"
  elif [ `expr match "$1" ".*\.c"` -gt 0 ]
  then
    SRC="$1"
  elif [ `expr match "$1" ".*\.S"` -gt 0 ]
  then
    SRC="$1"
    FROM_ASM=1
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
  elif [ `expr match "$1" "-m64"` -gt 0 ]
  then
    PLATFORM="x86-64"
    ARGS="$ARGS $1"
  elif [ `expr match "$1" "-m32"` -gt 0 ]
  then
    PLATFORM="x86"
    ARGS="$ARGS $1"
  elif [ `expr match "$1" ".*\.[ao]\$"` -gt 0 ]
  then
    LD_MODE=1
  elif [ `expr match "$1" "-O0"` -gt 0 ]
  then
    OX=$1
    OPT_OX=
  elif [ `expr match "$1" "-O"` -gt 0 ]
  then
    OX=$1
    OPT_OX=$1
  elif [ `expr match "$1" "-fPIC"` -gt 0 ]
  then
    FPIC=-fPIC
    LLC_PIC=-relocation-model=pic
  elif [ `expr match "$1" "-g"` -gt 0 ]
  then
    DEBUG=-g
  elif [ `expr match "$1" "-c\|-std=c++0x\|-Werror\|-finstrument-functions\|-Wno"` -gt 0 ]
  then
    if [ "$COMPILE_FAST" != "1" ]
    then
      echo "Dropped arg: $1"
    fi
  else
    ARGS="$ARGS $1"
  fi
  shift
done

if [ "$LD_MODE" == "1" ]
then
  PLATFORM=$PLATFORM $SCRIPT_ROOT/ld.sh $ALL_ARGS
  exit
fi

#SRC=$1
#echo $ARGS
FNAME=`echo $SRC | sed 's/\.[^.]*$//'`
SRC_BIT="$FNAME.ll"
SRC_TMP="$FNAME-tmp.ll"
SRC_INSTR="$FNAME-instr.ll"
SRC_ASM="$FNAME.S"
if [ -z $SRC_OBJ ]
then
  SRC_OBJ=`basename $FNAME.o`
fi
SRC_EXE="$FNAME"

INST_MODE=-offline
INST_MODE=-online

LOG=instrumentation.log

set_platform_dependent_vars

if [ "$FROM_ASM" != "1" ]
then
  if [ "$COMPILE_FAST" == "1" ]
  then
  # Translate C code to LLVM bitcode.
  $COMPILER -emit-llvm $MARCH $SRC $OX $FPIC $DEBUG -c $DA_FLAGS $ARGS -o "$SRC_BIT" ||   exit 1
  # Instrument the bitcode.
  $OPT $OPT_PASSES -load "$PASS_SO" $INST_MODE -arch=$XARCH "$SRC_BIT" -o "$SRC_INSTR" 2>$LOG || exit 1
  else
  # Translate C code to LLVM bitcode.
  $COMPILER -emit-llvm $MARCH $SRC $OX $FPIC $DEBUG -S $DA_FLAGS $ARGS -o "$SRC_BIT" || exit 1
  # Instrument the bitcode.
  $OPT $OPT_PASSES  "$SRC_BIT" $FPIC -S  > "$SRC_TMP" 2>$LOG || exit 1
  $OPT -load "$PASS_SO" $INST_MODE -arch=$XARCH $FPIC "$SRC_TMP" -S  > "$SRC_INSTR" 2>$LOG || exit 1
  fi

  # Translate LLVM bitcode to native assembly code.
  $LLC -march=$XARCH $LLC_PIC $OX $SRC_INSTR  -o $SRC_ASM
  if [ "$?" != "0" ]
  then
    $FALLBACK_COMPILER $MARCH $SRC $OX $FPIC $DEBUG -c $DA_FLAGS $ARGS -o $SRC_OBJ
    exit 0
  fi
fi

# Compile the object file.
$COMPILER $MARCH -c $SRC_ASM $OX $FPIC $DEBUG -o $SRC_OBJ || $FALLBACK_COMPILER $MARCH $SRC $OX $FPIC $DEBUG -c $DA_FLAGS $ARGS -o $SRC_OBJ ||   exit 1

