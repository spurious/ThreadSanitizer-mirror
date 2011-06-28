#!/bin/bash
function printf() {
  if [ "$GCCTSAN_DBG_SCRIPT" != "" ]; then
    echo GCCTSAN: $@
  fi
}

if [ "$GCCTSAN_GCC_DIR" == "" ]; then
  echo "set GCCTSAN_GCC_DIR before using the script (export GCCTSAN_GCC_DIR=/home/mrx/gcc-4.5.3/install)"
  exit 1
fi

if [ "$GCCTSAN_GCC_VER" == "" ]; then
  echo "set GCCTSAN_GCC_VER before using the script (export GCCTSAN_GCC_VER=4.5.2)"
  exit 1
fi

printf cc.sh "$@"

VER=$GCCTSAN_GCC_VER
GCC=$GCCTSAN_GCC_DIR/bin/$1
LIB32_PATH=$GCCTSAN_GCC_DIR/lib32
LIB64_PATH=$GCCTSAN_GCC_DIR/lib64
LNK=/usr/bin/$1
RTH=`dirname $0`/../include/relite_rt.h
PLG_NAME=librelite_$VER
PLG=`dirname $0`/../lib/$PLG_NAME.so
RTL32=`dirname $0`/../../tsan_rtl/tsan_rtl32.a
RTL64=`dirname $0`/../../tsan_rtl/tsan_rtl64.a
LNK_SCRIPT=`dirname $0`/../../tsan_rtl/link_config.txt
shift

wrap () {
  ARGS_LD+="-Wl,--wrap=$1 "
}

undefined() {
  ARGS_LD+=""
}

ARGS_LD=""
LIB_INSERTED=""
SHARED=""
PREPROCESS=""
M32=""
LINK=""
ASM=""

source $LNK_SCRIPT

function parse_args() {
until [ -z "$1" ]; do
  if [ "$1" == "-shared" ]; then
    SHARED="1"
  fi
  if [ "$1" == "-m32" ]; then
    M32="1"
  fi
  if [ "$1" == "-E" ]; then
    PREPROCESS="1"
  fi
  if [ "${1%.S}.S" == "$1" ]; then
    ASM="1"
  fi
  if [ "${1%.a}.a" == "$1" ]; then
    LINK="1"
  fi
  if [ "${1%.o}.o" == "$1" ]; then
    if [ `expr substr "$1" 1 2` != "-o" ]; then
      if [ "$PREV_O" == "" ]; then
        LINK="1"
      fi
    fi
  fi

  if [ "$1" == "-o" ]; then
    PREV_O="1"
  else 
    PREV_O=""
  fi

  if [ `expr substr "$1" 1 2` == "-l" ]; then
    if [ "$LIB_INSERTED" == "" ]; then
      LIB_INSERTED="1"
      if [ "$M32" == "1" ]; then
        ARGS_LD+=" -Wl,--undefined=dynamic_annotations_enforce_linking $RTL32 -lrt -lpthread"
      else
        ARGS_LD+=" -Wl,--undefined=dynamic_annotations_enforce_linking $RTL64 -lrt -lpthread"
      fi
    fi
  fi

  ARGS_LD+=" $1"
  shift
done

if [ "$LIB_INSERTED" == "" ]; then
  if [ "$M32" == "1" ]; then
    ARGS_LD+=" -Wl,--undefined=dynamic_annotations_enforce_linking $RTL32 -lrt -lpthread"
  else
    ARGS_LD+=" -Wl,--undefined=dynamic_annotations_enforce_linking $RTL64 -lrt -lpthread"
  fi
fi

if [ "$M32" == "1" ]; then
  ARGS_LD+=" -L$LIB32_PATH"
else
  ARGS_LD+=" -L$LIB64_PATH"
fi
}

parse_args "$@"

printf SHARED="$SHARED" PREPROCESS="$PREPROCESS" M32="$M32" LINK="$LINK" ASM="$ASM"
if [ "$LINK" != "" ]; then
  if [ "$SHARED" == "" ]; then
    printf $LNK $ARGS_LD
    $LNK $ARGS_LD
  else
    printf $LNK "$@" -L$LIB32_PATH -L$LIB64_PATH
    $LNK "$@" -L$LIB32_PATH -L$LIB64_PATH
  fi
else
  if [ "$ASM" != "" ]; then
    printf $LNK "$@"
    $LNK "$@"
  else
    if [ "$PREPROCESS" != "" ]; then
      printf $LNK "$@"
      $LNK "$@"
    else
      printf $GCC -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK -DDYNAMIC_ANNOTATIONS_PREFIX=LLVM -fplugin=$PLG -fplugin-arg-$PLG_NAME-ignore="$GCCTSAN_IGNORE" -include$RTH $GCCTSAN_ARGS "$@" -O1 -fno-builtin -fno-inline -fno-optimize-sibling-calls -fno-exceptions -g -fvisibility=default -w
      $GCC -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK -DDYNAMIC_ANNOTATIONS_PREFIX=LLVM -fplugin=$PLG -fplugin-arg-$PLG_NAME-ignore="$GCCTSAN_IGNORE" -include$RTH $GCCTSAN_ARGS "$@" -O1 -fno-builtin -fno-inline -fno-optimize-sibling-calls -fno-exceptions -g -fvisibility=default -w
    fi
  fi
fi



