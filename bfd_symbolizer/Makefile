CFLAGS+=-Wall

ifeq ($(BINUTILS_VER), )
  BINUTILS_VER=2.21
endif

ifeq ($(DEBUG), )
  CFLAGS+=-O3 -DNDEBUG
else
  CFLAGS+=-g3 -D_DEBUG
endif

all:    test32 test64

bfds64.a: bfd_symbolizer.c bfd_symbolizer.h ./binutils64/bin/lib/libbfd.a
	$(CC) -c bfd_symbolizer.c -o bfd_symbolizer64.o $(CFLAGS) -m64 -I./binutils64/bin/include -I./binutils64/binutils-$(BINUTILS_VER)/include
	rm -Rf ./binutils64/libbfd && mkdir ./binutils64/libbfd && cd ./binutils64/libbfd && ar x ./../bin/lib/libbfd.a
	rm -Rf ./binutils64/libiberty && mkdir ./binutils64/libiberty && cd ./binutils64/libiberty && ar x ./../bin/lib/libiberty.a
	rm -Rf ./binutils64/libz && mkdir ./binutils64/libz && cd ./binutils64/libz && ar x /usr/lib64/libz.a
	mv ./binutils64/libz/compress.o ./binutils64/libz/compress2.o
	rm -f bfds64.a
	ar cru bfds64.a bfd_symbolizer64.o ./binutils64/libbfd/*.o ./binutils64/libiberty/*.o ./binutils64/libz/*.o
	ranlib bfds64.a

bfds32.a: bfd_symbolizer.c bfd_symbolizer.h ./binutils32/bin/lib/libbfd.a
	$(CC) -c bfd_symbolizer.c -o bfd_symbolizer32.o $(CFLAGS) -m32 -I./binutils32/bin/include -I./binutils64/binutils-$(BINUTILS_VER)/include
	rm -Rf ./binutils32/libbfd && mkdir ./binutils32/libbfd && cd ./binutils32/libbfd && ar x ./../bin/lib/libbfd.a
	rm -Rf ./binutils32/libiberty && mkdir ./binutils32/libiberty && cd ./binutils32/libiberty && ar x ./../bin/lib32/libiberty.a
	rm -Rf ./binutils32/libz && mkdir ./binutils32/libz && cd ./binutils32/libz && ar x /usr/lib32/libz.a
	mv ./binutils32/libz/compress.o ./binutils32/libz/compress2.o
	rm -f bfds32.a
	ar cru bfds32.a bfd_symbolizer32.o ./binutils32/libbfd/*.o ./binutils32/libiberty/*.o ./binutils32/libz/*.o
	ranlib bfds32.a

./binutils64/bin/lib/libbfd.a: ./binutils-$(BINUTILS_VER).tar.bz2
	rm -Rf ./binutils64 && mkdir ./binutils64
	cd ./binutils64 && tar -jxvf ./../binutils-$(BINUTILS_VER).tar.bz2
	cd ./binutils64/binutils-$(BINUTILS_VER) && CFLAGS="-m64" CPPFLAGS="-m64" LDFLAGS="-m64" ./configure --build=amd64-unknown-linux-gnu --prefix=`pwd`/../bin
	cd ./binutils64/binutils-$(BINUTILS_VER) && make
	cd ./binutils64/binutils-$(BINUTILS_VER) && make install

./binutils32/bin/lib/libbfd.a: ./binutils-$(BINUTILS_VER).tar.bz2
	rm -Rf ./binutils32 && mkdir ./binutils32
	cd ./binutils32 && tar -jxvf ./../binutils-$(BINUTILS_VER).tar.bz2
	cd ./binutils32/binutils-$(BINUTILS_VER) && CFLAGS="-m32" CPPFLAGS="-m32" LDFLAGS="-m32" ./configure --build=i386-unknown-linux-gnu --prefix=`pwd`/../bin
	cd ./binutils32/binutils-$(BINUTILS_VER) && make
	cd ./binutils32/binutils-$(BINUTILS_VER) && make install

./binutils-$(BINUTILS_VER).tar.bz2:
	sudo apt-get install lib32z1-dev
	wget http://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VER).tar.bz2

test64: bfds64.a bfd_symbolizer.h test.cc test_dyn.cc test_dyn2.cc
	$(CXX) test_dyn.cc -o libsta64.so -Wall -g3 -fPIC -shared -m64
	$(CXX) test_dyn2.cc -o libdyn64.so -Wall -g3 -fPIC -shared -m64
	$(CXX) -m64 test.cc bfds64.a -o test64 -Wall -g3 -lpthread -ldl -L. -lsta64

test32: bfds32.a bfd_symbolizer.h test.cc test_dyn.cc test_dyn2.cc
	$(CXX) test_dyn.cc -o libsta32.so -Wall -g3 -fPIC -shared -m32
	$(CXX) test_dyn2.cc -o libdyn32.so -Wall -g3 -fPIC -shared -m32
	$(CXX) -m32 test.cc bfds32.a -o test32 -Wall -g3 -lpthread -ldl -L. -lsta32

clean:
	rm -f bfds32.a
	rm -f bfds64.a
	rm -f test32
	rm -f test64
	rm -f *.so
	rm -f *.o


