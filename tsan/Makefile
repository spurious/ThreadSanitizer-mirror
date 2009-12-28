VALGRIND_SRC_ROOT=../third_party/valgrind
VALGRIND_INST_ROOT=/tmp/tsan_inst
STLPORT_ROOT=../third_party/stlport
GOOGLETEST_ROOT=../third_party/googletest

CC=gcc
COPT=-O -fno-omit-frame-pointer
CWARN=-Wall -Wmissing-prototypes -Wshadow -Wpointer-arith -Wstrict-prototypes \
      -Wmissing-declarations -Wno-format-zero-length  \
      -fno-strict-aliasing -Wno-long-long -Wno-pointer-sign \
      -Wdeclaration-after-statement
CFLAGS=-g $(COPT) $(CWARN) -fno-strict-aliasing -fno-stack-protector -fpic
CXX=g++
CXXOPT=-O2
CXXFLAGS=$(CXXOPT) -g -Wall -fno-exceptions -Wvla
LD=g++
LDFLAGS=

VG_CXXFLAGS=-fno-rtti -fno-stack-protector
VG_INCLUDES=-I$(VALGRIND_SRC_ROOT) -I$(VALGRIND_SRC_ROOT)/include -I$(VALGRIND_SRC_ROOT)/VEX/pub -I$(STLPORT_ROOT)
VG_DEFINES=-D_STLP_NO_IOSTREAMS=1 -DTS_VALGRIND=1


OS=   # linux, darwin
ARCH= # x86, amd64
D=    # <empty>, -debug
ARCHOS=$(ARCH)-$(OS)
ARCH_OS=$(ARCH)_$(OS)

ifeq ($(DEBUG), 1)
  D=-debug
  DEFINES=-DDEBUG=1
else
  D=
  DEFINES=-DINCLUDE_THREAD_SANITIZER_CC=1
endif

ifeq ($(ARCH), x86)
  ARCHFLAGS=-m32
else ifeq ($(ARCH), amd64)
  ARCHFLAGS=-m64
else
  ARCH=UNKNOWN_ARCH
endif

ifeq ($(OS), linux)
else ifeq ($(OS), darwin)
else
  OS=UNKNOWN_OS
endif

VALGRIND_MACROS=-DVGA_$(ARCH)=1 -DVGO_$(OS)=1 -DVGP_$(ARCH_OS)=1

VALGRIND_LIBS=$(VALGRIND_SRC_ROOT)/coregrind/libcoregrind-$(ARCHOS).a \
		  $(VALGRIND_SRC_ROOT)/VEX/libvex-$(ARCHOS).a


dummy:

all: tsan_vg tsan_offline test

l: l32 l64
l64: l64o l64d
l32: l32o l32d
ld: l64d l32d
lo: l64o l32o

l64d:
	$(MAKE) all OS=linux ARCH=amd64  DEBUG=1
l64o:
	$(MAKE) all OS=linux ARCH=amd64  DEBUG=0

l32d:
	$(MAKE) all OS=linux ARCH=x86  DEBUG=1
l32o:
	$(MAKE) all OS=linux ARCH=x86  DEBUG=0

install_l: l
	cp -v bin/tsan* bin/vgpreload_tsan* $(VALGRIND_INST_ROOT)/lib/valgrind


tsan_vg:  bin bin/tsan$D-$(ARCHOS) bin/vgpreload_tsan$D-$(ARCHOS).so

bin:
	mkdir -p bin

TS_HEADERS=thread_sanitizer.h ts_util.h suppressions.h
TS_VG_HEADERS=ts_valgrind.h ts_valgrind_client_requests.h ts_wrap_and_ignore.h

bin/$(ARCHOS)$(D)-vg-%.o: %.cc $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CXX) $(CXXFLAGS) $(VG_CXXFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VALGRIND_MACROS) -o $@ -c $< $(DEFINES) $(VG_DEFINES)

bin/$(ARCHOS)$(D)-%.o: %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -o $@ -c $< $(DEFINES)

bin/${ARCHOS}$(D)-gtest-%.o: %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -I$(GOOGLETEST_ROOT)/include -o $@ -c $<

bin/$(ARCHOS)$(D)-preload-%.o: %.c $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CC) $(CFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VALGRIND_MACROS) -o $@ -c $<

bin/tsan$(D)-$(ARCHOS): bin/$(ARCHOS)$(D)-vg-thread_sanitizer.o bin/$(ARCHOS)$(D)-vg-ts_valgrind.o bin/$(ARCHOS)$(D)-vg-ts_util.o
	$(LD) $(LDFLAGS) $(ARCHFLAGS) -static -Wl,-defsym,valt_load_address=0x38000000 -nodefaultlibs -nostartfiles \
		-u _start  -Wl,-T,$(VALGRIND_SRC_ROOT)/valt_load_address_$(ARCH_OS).lds  -o $@ $^ \
		$(VALGRIND_LIBS) -lgcc

bin/vgpreload_tsan$(D)-$(ARCHOS).so: bin/$(ARCHOS)$(D)-preload-ts_valgrind_intercepts.o
	$(LD) $(LDFLAGS)  $(ARCHFLAGS) -nodefaultlibs -shared -Wl,-z,interpose,-z,initfirst -o $@  $<

tsan_offline: bin bin/ts_offline_$(ARCHOS)
	
bin/ts_offline_$(ARCHOS): bin/$(ARCHOS)$(D)-ts_offline.o bin/$(ARCHOS)$(D)-thread_sanitizer.o bin/$(ARCHOS)$(D)-ts_util.o
	$(LD) $(LDFLAGS) $(ARCHFLAGS) -o $@ $^

test: bin bin/suppressions_test_${ARCHOS}

bin/suppressions_test_${ARCHOS}: bin/${ARCHOS}$(D)-gtest-suppressions_test.o bin/$(ARCHOS)$(D)-suppressions.o bin/$(ARCHOS)$(D)-ts_util.o $(GOOGLETEST_ROOT)/make/gtest_main.a
	$(LD) $(LDFLAGS) -o $@ $^


clean:
	rm -vrf *.o *.so bin
