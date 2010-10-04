#!/bin/bash
#
# An ar wrapper that simply calls ar and puts together all the debug
# information for the object files.

set -x
HERE=`dirname $0`

AR=ar
LINK_DBG="$HERE/link_debuginfo.sh"

until [ -z "$1" ]
do
  if [ `expr match "$1" ".*\.o"` -gt 0 ]
  then
    DBG_FILES="$DBG_FILES $1.dbg"
    ARGS="$ARGS $1"
  elif [ `expr match "$1" ".*\.a"` -gt 0 ]
  then
    LIB_DBG="$1.dbg"
    ARGS="$ARGS $1"
  else
    ARGS="$ARGS $1"
  fi
  shift
done

${AR} ${ARGS}
${LINK_DBG} ${DBG_FILES} -o ${LIB_DBG}
