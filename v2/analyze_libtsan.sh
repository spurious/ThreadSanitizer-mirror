#!/bin/bash

get_asm() {
  objdump -d tsan/libtsan.a | grep $1.: -A 10000 | \
    awk "/[^:]$/ {print;} />:/ {c++; if (c == 2) {exit}}"
}

list="tsan_write1 \
      tsan_write2 \
      tsan_write4 \
      tsan_write8 \
      tsan_read1 \
      tsan_read2 \
      tsan_read4 \
      tsan_read8"


for f in $list; do
  file=asm_$f.s
  get_asm $f > $file
  tot=$(wc -l < $file)
  rsp=$(grep '(%rsp)' $file | wc -l)
  printf "%11s total %d rsp %d\n" $f $tot $rsp;
done
