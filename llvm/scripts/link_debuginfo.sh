#!/bin/bash
#
# Copyright 2010 Google Inc. All Rights Reserved.
# Author: glider@google.com (Alexander Potapenko)

set -x

TMP=`tempfile`

until [ -z "$1" ]
do
  if [ `expr match "$1" "-o"` -gt 0 ]
  then
    if [ "$1" == "-o" ]
    then
      # "-o fname" -- 2 arguments
      shift
      SRC_OUT="$1"
    else
      # "-ofname" -- 1 argument
      SRC_OUT=${1:2}
    fi
  elif [ `expr match "$1" ".*\.dbg"` -gt 0 ]
  then
    cat $1 >> $TMP
  else
    ARGS="$ARGS $1"
  fi
  shift
done

mv $TMP $SRC_OUT
