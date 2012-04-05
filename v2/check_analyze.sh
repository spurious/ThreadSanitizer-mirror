#!/bin/bash
set -u

./analyze_libtsan.sh >analyze.res
cat analyze.res

mops="write1 \
      write2 \
      write4 \
      write8 \
      read1 \
      read2 \
      read4 \
      read8"
func="func_entry \
      func_exit"

check() {
  res=$(egrep "$1 .* $2 $3; " analyze.res)
  if [ "$res" == "" ]; then
    echo FAILED $1 must contain $2 $3
    exit 1
  fi
}

for f in $mops; do
  check $f rsp 1   # To read caller pc.
  check $f push 0
  check $f pop 0
done

for f in $func; do
  check $f rsp 0
  check $f push 0
  check $f pop 0
  check $f call 1  # TraceSwitch()
done

echo looks fine
