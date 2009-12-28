#!/bin/bash

PIN_ROOT=${PIN_ROOT:-$HOME/pin}
PIN_BINARY=${PIN_BINARY:-pin}
TS_ROOT=${TS_ROOT:-`pwd`/bin}
TS_VARIANT=-debug

export MSM_THREAD_SANITIZER=1


FOLLOW=-follow_execv
PIN_FLAGS=${PIN_FLAGS:-""}

#TS_FLAGS="-short-name 1 TS_ARGS_BEG $0 \
#         --file_prefix_to_cut=/google3/ \
#         --file_prefix_to_cut=/drt/trunk/unittest/ \
#         --file_prefix_to_cut=/proc/self/cwd/ \
#         --ignore_routines=$TS_ROOT/ts_ignore_rtn.txt"
TS_FLAGS="-short_name"
PIN_FLAGS=""

VERBOZE=0

for arg in "$@"; do
  case $arg in
    --trace_children=yes) PIN_FLAGS="$PIN_FLAGS $FOLLOW";;
    --trace-children=yes) PIN_FLAGS="$PIN_FLAGS $FOLLOW";;
    --opt) TS_VARIANT="";;
    --deb) TS_VARIANT="-debug";;
    --v=[1-9]) VERBOZE=1; TS_FLAGS="$TS_FLAGS $arg";;
    --) shift; break;;
    -*) TS_FLAGS="$TS_FLAGS $arg";;
    *) break;;
  esac
  shift
done


ulimit -c 0 # core make very little sense here

if [ $VERBOZE == "1" ] ; then
  printf "PIN_ROOT   : %s\n" "$PIN_ROOT"
  printf "PIN_BINARY : %s\n" "$PIN_BINARY"
  printf "PIN_FLAGS  : %s\n" "$PIN_FLAGS"
  printf "TS_ROOT    : %s\n" "$TS_ROOT"
  printf "TS_VARIANT : %s\n" "$TS_VARIANT"
  printf "TS_FLAGS   : %s\n" "$TS_FLAGS"
  printf "PARAMS     : %s\n" "$*"
fi

TS_PARAMS="$@"

run() {
  echo $@
  $@
}

run $PIN_ROOT/$PIN_BINARY $PIN_FLAGS \
  -t64 $TS_ROOT/amd64-linux${TS_VARIANT}-ts_pin.so \
  -t   $TS_ROOT/x86-linux${TS_VARIANT}-ts_pin.so \
 $TS_FLAGS -- $TS_PARAMS

