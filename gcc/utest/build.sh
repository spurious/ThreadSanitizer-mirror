${GCCTSAN_GCC_BIN} -c -fno-inline -fno-exceptions -fplugin=../plg/Debug/libgcctsan.so -include../rt/gcctsan_rt.h main.cc
${GCCTSAN_GCC_BIN} -o test -lpthread -lstdc++ -l../rt/Debug/libgcctsanrt.a main.o


