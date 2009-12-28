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
VG_DEFINES=-DVGA_$(ARCH)=1 -DVGO_$(OS)=1 -DVGP_$(ARCH_OS)=1 -D_STLP_NO_IOSTREAMS=1 -DTS_VALGRIND=1
VG_INCLUDES=-I$(VALGRIND_SRC_ROOT) -I$(VALGRIND_SRC_ROOT)/include -I$(VALGRIND_SRC_ROOT)/VEX/pub -I$(STLPORT_ROOT)

PIN_CXXFLAGS=-fPIC
PIN_DEFINES=-DBIGARRAY_MULTIPLIER=1 -DUSING_XED  -DTARGET_$(PIN_ARCH2)  -DHOST_$(PIN_ARCH2)   -DTARGET_LINUX
PIN_INCLUDES=-I$(PIN_ROOT)/source/include -I$(PIN_ROOT)/source/include/gen -I$(PIN_ROOT)/extras/xed2-$(PIN_ARCH)/include
PIN_LIBPATHS=-L$(PIN_ROOT)/extras/xed2-$(PIN_ARCH)/lib -L$(PIN_ROOT)/$(PIN_ARCH)/lib -L$(PIN_ROOT)/$(PIN_ARCH)/lib-ext
PIN_LDFLAGS=-g -shared -Wl,-Bsymbolic -Wl,--version-script=$(PIN_ROOT)/source/include/pintool.ver
PIN_LIBS= -lpin  -lxed -ldwarf -lelf -ldl -lpthread

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
  PIN_ARCH=ia32
  PIN_ARCH2=IA32
else ifeq ($(ARCH), amd64)
  ARCHFLAGS=-m64
  PIN_ARCH=intel64
  PIN_ARCH2=IA32E
else
  ARCH=UNKNOWN_ARCH
endif

ifeq ($(OS), linux)
  SO=so
  OBJ=o
else ifeq ($(OS), darwin)
  SO=so
  OBJ=o
else ifeq ($(OS), windows)
  SO=dll
  OBJ=obj
else
  OS=UNKNOWN_OS
endif



VALGRIND_LIBS=$(VALGRIND_SRC_ROOT)/coregrind/libcoregrind-$(ARCHOS).a \
		  $(VALGRIND_SRC_ROOT)/VEX/libvex-$(ARCHOS).a

# prefix of binary files
P=bin/$(ARCHOS)$(D)-

dummy:
	@echo "For 64-bit Linux build: make l64"
	@echo "For 32-bit Linux build: make l32"
	@echo "For mixed  Linux build: make l"

all: tsan_valgrind tsan_pin tsan_offline test

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


tsan_valgrind:  bin bin/tsan$D-$(ARCHOS) bin/vgpreload_tsan$D-$(ARCHOS).so

bin:
	mkdir -p bin

TS_HEADERS=thread_sanitizer.h ts_util.h suppressions.h
TS_VG_HEADERS=ts_valgrind.h ts_valgrind_client_requests.h ts_wrap_and_ignore.h

$(P)%.o: %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -o $@ -c $< $(DEFINES)

$(P)vg-%.o: %.cc $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CXX) $(CXXFLAGS) $(VG_CXXFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VG_DEFINES) -o $@ -c $< $(DEFINES)

$(P)pin-%.o: %.cc $(TS_HEADERS) $(TS_PIN_HEADERS)
	$(CXX) $(CXXFLAGS) $(PIN_CXXFLAGS) $(ARCHFLAGS) $(PIN_INCLUDES) $(PIN_DEFINES) -o $@ -c $< $(DEFINES)

$(P)gtest-%.o: %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -I$(GOOGLETEST_ROOT)/include -o $@ -c $<

$(P)preload-%.o: %.c $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CC) $(CFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VG_DEFINES) -o $@ -c $<

bin/tsan$(D)-$(ARCHOS): $(P)vg-thread_sanitizer.o $(P)vg-ts_valgrind.o $(P)vg-ts_util.o
	$(LD) $(LDFLAGS) $(ARCHFLAGS) -static -Wl,-defsym,valt_load_address=0x38000000 -nodefaultlibs -nostartfiles \
		-u _start  -Wl,-T,$(VALGRIND_SRC_ROOT)/valt_load_address_$(ARCH_OS).lds  -o $@ $^ \
		$(VALGRIND_LIBS) -lgcc

bin/vgpreload_tsan$(D)-$(ARCHOS).so: $(P)preload-ts_valgrind_intercepts.o
	$(LD) $(LDFLAGS)  $(ARCHFLAGS) -nodefaultlibs -shared -Wl,-z,interpose,-z,initfirst -o $@  $<

tsan_offline: bin bin/ts_offline_$(ARCHOS)
	
bin/ts_offline_$(ARCHOS): $(P)ts_offline.o $(P)thread_sanitizer.o $(P)ts_util.o
	$(LD) $(LDFLAGS) $(ARCHFLAGS) -o $@ $^

test: bin bin/suppressions_test_${ARCHOS}$(D)

bin/suppressions_test_${ARCHOS}$(D): $(P)gtest-suppressions_test.o $(P)suppressions.o $(P)ts_util.o $(GOOGLETEST_ROOT)/make/gtest_main_${ARCH}.a
	$(LD) $(LDFLAGS) $(ARCHFLAGS) -o $@ $^

$(GOOGLETEST_ROOT)/make/gtest_main_$(ARCH).a:
	cd $(GOOGLETEST_ROOT)/make && $(MAKE) clean && $(MAKE) CXXFLAGS=$(ARCHFLAGS) && mv gtest_main.a gtest_main_$(ARCH).a


TS_PIN_OBJECTS=$(P)pin-ts_pin.$(OBJ) $(P)pin-ts_util.$(OBJ) $(P)pin-thread_sanitizer.$(OBJ)



$(P)ts_pin.so: $(TS_PIN_OBJECTS)
	$(LD) $(ARCHFLAGS) $(PIN_LDFLAGS) $(PIN_LIBPATHS) -o $@ $^  $(PIN_LIBS)

ifeq ($(PIN_ROOT), )
tsan_pin:
	@echo "Set PIN_ROOT to build the PIN-based version"
else
tsan_pin: $(P)ts_pin.$(SO)
endif

clean:
	rm -rfv bin core* pintool.log* pin.log
