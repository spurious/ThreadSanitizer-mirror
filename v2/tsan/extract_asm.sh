objdump -d libtsan.a | grep __tsan_write4.: -A 10000 | awk "/[^:]$/ {print;} />:/ {c++; if (c == 2) {exit}}"
