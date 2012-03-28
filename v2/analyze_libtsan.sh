#!/bin/bash

get_asm() {
  objdump -d tsan/libtsan.a | grep tsan_$1.: -A 10000 | \
    awk "/[^:]$/ {print;} />:/ {c++; if (c == 2) {exit}}"
}

list="write1 \
      write2 \
      write4 \
      write8 \
      read1 \
      read2 \
      read4 \
      read8"


for f in $list; do
  file=asm_$f.s
  get_asm $f > $file
  tot=$(wc -l < $file)
  rsp=$(grep '(%rsp)' $file | wc -l)
  call=$(grep 'call' $file | wc -l)
  mov=$(grep 'mov' $file | wc -l)
  lea=$(grep 'lea' $file | wc -l)
  ud2=$(grep 'ud2' $file | wc -l)
  sh=$(grep 'shr\|shl' $file | wc -l)
  cmp=$(grep 'cmp\|test' $file | wc -l)
  printf "%6s tot %d rsp %2d call %d sh %3d mov %d lea %d ud2 %d cmp %d\n" \
    $f $tot $rsp $call $sh $mov $lea $ud2 $cmp;
done
