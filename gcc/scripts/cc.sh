#!/bin/bash
echo RELITE: cc.sh "$@"

VER=4.5.3
GCC=`dirname $0`/../../third_party/gcc-$VER/install/bin/$1
LNK=/usr/bin/$1
RTH=`dirname $0`/../plg/relite_rt.h
PLG_NAME=librelite_$VER
PLG=`dirname $0`/../bin/$PLG_NAME.so
RTL32=`dirname $0`/../../llvm/tsan_rtl/tsan_rtl32.a
RTL64=`dirname $0`/../../llvm/tsan_rtl/tsan_rtl64.a
LNK_SCRIPT=`dirname $0`/../../llvm/scripts/link_config.txt
shift

wrap () {
  ARGS_LD+="-Wl,--wrap=$1 "
}

undefined() {
  ARGS_LD+=""
}

ARGS=""
ARGS_CC=""
ARGS_LD=""
LIB_INSERTED=""
SHARED=""
M32=""
LINK=""

source $LNK_SCRIPT

until [ -z "$1" ]; do
  if [ "$1" == "-shared" ]; then
    SHARED="1"
  fi
  if [ "$1" == "-m32" ]; then
    M32="1"
  fi
  if [ "$1" == "-lpthread" ]; then
    LINK="1"
  fi
  if [ "$1" == "-lrt" ]; then
    LINK="1"
  fi
  if [ "$1" == "start-group" ]; then
    LINK="1"
  fi
  if [ "${1%.a}.a" == "$1" ]; then
    LINK="1"
  fi

  if [ `expr substr "$1" 1 2` == "-l" ]; then
    if [ "$LIB_INSERTED" == "" ]; then
      LIB_INSERTED="1"
      if [ "$M32" == "1" ]; then
        ARGS_LD+=" $RTL32 -lrt -lpthread"
      else
        ARGS_LD+=" $RTL64 -lrt -lpthread"
      fi
    fi
  fi
  ARGS+=" $1"
  ARGS_CC+=" $1"
  ARGS_LD+=" $1"
  shift
done

if [ "$LIB_INSERTED" == "" ]; then
  if [ "$M32" == "1" ]; then
    ARGS_LD+=" $RTL32 -lrt -lpthread"
  else
    ARGS_LD+=" $RTL64 -lrt -lpthread"
  fi
fi

if [ "$LINK" == "" ]; then
  echo RELITE: $GCC -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK -DDYNAMIC_ANNOTATIONS_PREFIX=LLVM -fplugin=$PLG -fplugin-arg-$PLG_NAME-ignore="$RELITE_IGNORE" -include$RTH $GCCTSAN_ARGS $ARGS_CC -O1 -fno-inline -fno-optimize-sibling-calls -fno-exceptions -g
  $GCC -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK -DDYNAMIC_ANNOTATIONS_PREFIX=LLVM -fplugin=$PLG -fplugin-arg-$PLG_NAME-ignore="$RELITE_IGNORE" -include$RTH $GCCTSAN_ARGS $ARGS_CC -O1 -fno-inline -fno-optimize-sibling-calls -fno-exceptions -g
else
  if [ "$SHARED" == "" ]; then
    echo RELITE: $LNK $ARGS_LD
    $LNK $ARGS_LD
  else
    echo RELITE: $LNK $ARGS
    $LNK $ARGS
  fi
fi




