${GCCTSAN_GCC_BIN} -c -fno-inline -fno-exceptions -fplugin=../plg/Debug/librelite.so -include../rt/relite_rt.h main.cc
${GCCTSAN_GCC_BIN} -o test -lpthread -lstdc++ -L../rt/Debug -lrelitert main.o


