CFLAGS+=-Wall

ifeq ($(BINUTILS_VER), )
  BINUTILS_VER=2.21
endif

ifeq ($(BFDS_DEBUG), )
  CFLAGS+=-O3 -DNDEBUG
else
  CFLAGS+=-g3 -D_DEBUG
endif

all:    test32 test64 testso32 testso64

# No targets should depend on this one, because it cannot be ran automatically.
# Instead run it on the target machine manually.
package_deps:
	sudo apt-get install lib32z1-dev

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
	$(CC) -c bfd_symbolizer.c -o bfd_symbolizer32.o $(CFLAGS) -m32 -I./binutils32/bin/include -I./binutils32/binutils-$(BINUTILS_VER)/include
	rm -Rf ./binutils32/libbfd && mkdir ./binutils32/libbfd && cd ./binutils32/libbfd && ar x ./../bin/lib/libbfd.a
	rm -Rf ./binutils32/libiberty && mkdir ./binutils32/libiberty && cd ./binutils32/libiberty && ar x ./../bin/lib32/libiberty.a
	rm -Rf ./binutils32/libz && mkdir ./binutils32/libz && cd ./binutils32/libz && ar x /usr/lib32/libz.a
	mv ./binutils32/libz/compress.o ./binutils32/libz/compress2.o
	rm -f bfds32.a
	ar cru bfds32.a bfd_symbolizer32.o ./binutils32/libbfd/*.o ./binutils32/libiberty/*.o ./binutils32/libz/*.o
	ranlib bfds32.a

libbfds32.so: bfd_symbolizer.c bfd_symbolizer.h ./binutils32so/bin/lib/libbfd.a
	$(CC) bfd_symbolizer.c -I./binutils32/bin/include -I./binutils32/binutils-$(BINUTILS_VER)/include $(CFLAGS) -shared -fPIC -m32 -o libbfds32.so ./binutils32so/bin/lib/libbfd.a ./binutils32so/bin/lib32/libiberty.a -lz

libbfds64.so: bfd_symbolizer.c bfd_symbolizer.h ./binutils64so/bin/lib/libbfd.a
	$(CC) bfd_symbolizer.c -I./binutils64/bin/include -I./binutils64/binutils-$(BINUTILS_VER)/include $(CFLAGS) -shared -fPIC -m64 -o libbfds64.so ./binutils64so/bin/lib/libbfd.a ./binutils64so/bin/lib/libiberty.a -lz

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

./binutils64so/bin/lib/libbfd.a: ./binutils-$(BINUTILS_VER).tar.bz2
	rm -Rf ./binutils64so && mkdir ./binutils64so
	cd ./binutils64so && tar -jxvf ./../binutils-$(BINUTILS_VER).tar.bz2
	cd ./binutils64so/binutils-$(BINUTILS_VER) && CFLAGS="-m64 -fPIC" CPPFLAGS="-m64 -fPIC" LDFLAGS="-m64" ./configure --build=amd64-unknown-linux-gnu --prefix=`pwd`/../bin
	cd ./binutils64so/binutils-$(BINUTILS_VER) && make
	cd ./binutils64so/binutils-$(BINUTILS_VER) && make install

./binutils32so/bin/lib/libbfd.a: ./binutils-$(BINUTILS_VER).tar.bz2
	rm -Rf ./binutils32so && mkdir ./binutils32so
	cd ./binutils32so && tar -jxvf ./../binutils-$(BINUTILS_VER).tar.bz2
	cd ./binutils32so/binutils-$(BINUTILS_VER) && CFLAGS="-m32 -fPIC" CPPFLAGS="-m32 -fPIC" LDFLAGS="-m32" ./configure --build=i386-unknown-linux-gnu --prefix=`pwd`/../bin
	cd ./binutils32so/binutils-$(BINUTILS_VER) && make
	cd ./binutils32so/binutils-$(BINUTILS_VER) && make install

./binutils-$(BINUTILS_VER).tar.bz2:
	wget http://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VER).tar.bz2

test64: bfds64.a bfd_symbolizer.h test.cc test_dyn.cc test_dyn2.cc
	$(CXX) test_dyn.cc -o libsta64.so -Wall -g3 -fPIC -shared -m64
	$(CXX) test_dyn2.cc -o libdyn64.so -Wall -g3 -fPIC -shared -m64
	$(CXX) -m64 test.cc bfds64.a -o test64 -Wall -g3 -lpthread -ldl -L. -lsta64

test32: bfds32.a bfd_symbolizer.h test.cc test_dyn.cc test_dyn2.cc
	$(CXX) test_dyn.cc -o libsta32.so -Wall -g3 -fPIC -shared -m32
	$(CXX) test_dyn2.cc -o libdyn32.so -Wall -g3 -fPIC -shared -m32
	$(CXX) -m32 test.cc bfds32.a -o test32 -Wall -g3 -lpthread -ldl -L. -lsta32

testso32: libbfds32.so bfd_symbolizer.h test_so.cc
	$(CXX) test_so.cc -o testso32 -Wall -g3 -m32 -L. -lbfds32 -ldl

testso64: libbfds64.so bfd_symbolizer.h test_so.cc
	$(CXX) test_so.cc -o testso64 -Wall -g3 -m64 -L. -lbfds64 -ldl

clean:
	rm -f test*.
	rm -f *.so
	rm -f *.a
	rm -f *.o
