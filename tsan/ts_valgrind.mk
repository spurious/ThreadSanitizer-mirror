VALGRIND_SRC_ROOT=../third_party/valgrind
VALGRIND_INST_ROOT=../vg_inst
STLPORT_ROOT=../third_party/stlport
CC=gcc
COPT=-O -fno-omit-frame-pointer
CWARN=-Wall -Wmissing-prototypes -Wshadow -Wpointer-arith -Wstrict-prototypes \
      -Wmissing-declarations -Wno-format-zero-length  \
      -fno-strict-aliasing -Wno-long-long -Wno-pointer-sign \
      -Wdeclaration-after-statement
CFLAGS=-g ${COPT} ${CWARN} -fno-strict-aliasing -fno-stack-protector -fpic
CXX=g++
CXXOPT=-O2
CXXFLAGS=$(CXXOPT) -g -fno-exceptions -Wall -fno-exceptions -fno-rtti -fno-stack-protector -Wvla -D_STLP_NO_IOSTREAMS=1 -DTS_VALGRIND=1
LD=g++
LDFLAGS=
INCLUDES=-I${VALGRIND_SRC_ROOT} -I${VALGRIND_SRC_ROOT}/include -I${VALGRIND_SRC_ROOT}/VEX/pub   -I${STLPORT_ROOT}


OS=  # linux, darwin
ARCH= # x86, amd64
ARCH_OS=${ARCH}_${OS}
ARCHOS=${ARCH}-${OS}
D=

ifeq (${DEBUG}, 1)
  D=-debug
  DEFINES=-DDEBUG=1
else
  D=
  DEFINES=-DINCLUDE_THREAD_SANITIZER_CC=1
endif

ifeq (${ARCH}, x86)
  ARCHFLAGS=-m32
else
  ARCHFLAGS=-m64
endif

VALGRIND_MACROS=-DVGA_${ARCH}=1 -DVGO_${OS}=1 -DVGP_${ARCH_OS}=1

VALGRIND_LIBS=${VALGRIND_SRC_ROOT}/coregrind/libcoregrind-${ARCHOS}.a \
		  ${VALGRIND_SRC_ROOT}/VEX/libvex-${ARCHOS}.a

all:

l: l32 l64
l64: l64o l64d
l32: l32o l32d
ld: l64d l32d
lo: l64o l32o

l64d:
	$(MAKE) tsan OS=linux ARCH=amd64  DEBUG=1
l64o:
	$(MAKE) tsan OS=linux ARCH=amd64  DEBUG=0

l32d:
	$(MAKE) tsan OS=linux ARCH=x86  DEBUG=1
l32o:
	$(MAKE) tsan OS=linux ARCH=x86  DEBUG=0

install:
	cp -v bin/tsan* bin/vgpreload_tsan* ${VALGRIND_INST_ROOT}/lib/valgrind


tsan:  bin bin/tsan$D-${ARCHOS} bin/vgpreload_tsan$D-${ARCHOS}.so

bin:
	mkdir -p bin

HEADERS=thread_sanitizer.h ts_util.h ts_valgrind.h \
	ts_valgrind_client_requests.h ts_wrap_and_ignore.h

bin/${ARCHOS}${D}-%.o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) ${ARCHFLAGS} $(INCLUDES) ${VALGRIND_MACROS} -o $@ -c $< ${DEFINES}

bin/${ARCHOS}${D}-preload-%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) ${ARCHFLAGS} $(INCLUDES) ${VALGRIND_MACROS} -o $@ -c $<

bin/tsan${D}-${ARCHOS}: bin/${ARCHOS}${D}-thread_sanitizer.o bin/${ARCHOS}${D}-ts_valgrind.o bin/${ARCHOS}${D}-ts_util.o
	$(LD) ${ARCHFLAGS} -static -Wl,-defsym,valt_load_address=0x38000000 -nodefaultlibs -nostartfiles \
		-u _start  -Wl,-T,${VALGRIND_SRC_ROOT}/valt_load_address_${ARCH_OS}.lds  -o $@ $^ \
		${VALGRIND_LIBS}		-lgcc

bin/vgpreload_tsan${D}-${ARCHOS}.so: bin/${ARCHOS}${D}-preload-ts_valgrind_intercepts.o
	$(LD) ${ARCHFLAGS} -nodefaultlibs -shared -Wl,-z,interpose,-z,initfirst -o $@  $<


clean:
	rm -vrf *.o *.so bin/
