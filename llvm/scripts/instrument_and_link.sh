#!/bin/bash

# A script for instrumenting test programs and linking them against
# ThreadSanitizer RTL.
# Usage:
#   $ ./instrument_and_link.sh <x86|x64> path/filename.c
# Output:
#   path/filename.ll -- the source file translated into LLVM IR.
#   path/filename-instr.ll -- LLVM IR with instrumentation added.
#   path/filename.S -- the instrumented IR compiled into native assembly code.
#   path/filename.o -- the object file.
#   path/filename -- the resulting binary.
#   path/filename.dbg -- RTL debug info for the program.

set -x
SCRIPT_ROOT=`dirname $0`

source "$SCRIPT_ROOT/common.sh"

PLATFORM=$1
set_platform_dependent_vars

SRC=$2
FNAME=`echo $SRC | sed 's/\.[^.]*$//'`
SRC_BIT="$FNAME.ll"
SRC_TMP="$FNAME-tmp.ll"
SRC_INSTR="$FNAME-instr.ll"
SRC_ASM="$FNAME.S"
SRC_OBJ="$FNAME.o"
SRC_EXE="${FNAME}"
SRC_DBG="$FNAME.dbg"

INST_MODE=-offline
INST_MODE=-online
CXXFLAGS=
OPT_PASSES=

source "$SCRIPT_ROOT/link_config.sh"

DEBUG=-g
#DEBUG=

LOG=instrumentation.log

OX=-O1

# Translate C code to LLVM IR.
$LLVM_GCC -emit-llvm $MARCH $SRC $DEBUG -S $CXXFLAGS -o "$SRC_BIT" || exit 1
# Instrument the IR.
$OPT $OPT_PASSES "$SRC_BIT" -S  > "$SRC_TMP" 2>$LOG || exit 1
$OPT -load "$PASS_SO" $INST_MODE -arch=$XARCH "$SRC_TMP" -S  > "$SRC_INSTR" 2>$LOG || exit 1
cat $LOG | grep "^->" | sed "s/^->//" > "$SRC_DBG"
cat $LOG | grep -v "^->"
# Translate LLVM IR to native assembly code.
$LLC -march=$XARCH $OX $SRC_INSTR  -o $SRC_ASM || exit 1
# Compile the object file.
$LLVM_GCC $MARCH -c $SRC_ASM $OX $DEBUG -o $SRC_OBJ
# Link with the mops_impl.o
$TSAN_LD $MARCH $DEBUG $SRC_OBJ $LDFLAGS $TSAN_RTL -o $SRC_EXE

