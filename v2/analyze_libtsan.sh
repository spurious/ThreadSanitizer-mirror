#!/bin/bash

set -e
set -u

get_asm() {
  grep tsan_$1.: -A 10000 libtsan.objdump | \
    awk "/[^:]$/ {print;} />:/ {c++; if (c == 2) {exit}}"
}

list="write1 \
      write2 \
      write4 \
      write8 \
      read1 \
      read2 \
      read4 \
      read8 \
      func_entry \
      func_exit"

BIN=`dirname $0`/tests/tsan_test
objdump -d $BIN  > libtsan.objdump
nm -S $BIN | grep "__tsan_" > libtsan.nm

for f in $list; do
  file=asm_$f.s
  get_asm $f > $file
  tot=$(wc -l < $file)
  size=$(grep $f$ libtsan.nm | awk --non-decimal-data '{print ("0x"$2)+0}')
  rsp=$(grep '(%rsp)' $file | wc -l)
  call=$(grep 'call' $file | wc -l)
  load=$(egrep 'mov .*\,.*\(.*\)|cmp .*\,.*\(.*\)' $file | wc -l)
  store=$(egrep 'mov .*\(.*\),' $file | wc -l)
  mov=$(grep 'mov' $file | wc -l)
  lea=$(grep 'lea' $file | wc -l)
  ud2=$(grep 'ud2' $file | wc -l)
  sh=$(grep 'shr\|shl' $file | wc -l)
  cmp=$(grep 'cmp\|test' $file | wc -l)
  printf "%10s tot %3d size %4d rsp %2d call %2d load %2d store %2d sh %3d mov %3d lea %3d ud2 %2d cmp %3d\n" \
    $f $tot $size $rsp $call $load $store $sh $mov $lea $ud2 $cmp;
done
