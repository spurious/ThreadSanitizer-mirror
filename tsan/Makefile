VALGRIND_ROOT=../third_party/valgrind
VALGRIND_INST_ROOT=/tmp/tsan_inst
STLPORT_ROOT=../third_party/stlport
GTEST_ROOT=../third_party/googletest
OFFLINE=1

CC=gcc
COPT=-O -fno-omit-frame-pointer
CWARN=-Wall -Wmissing-prototypes -Wshadow -Wpointer-arith -Wstrict-prototypes \
      -Wmissing-declarations -Wno-format-zero-length  \
      -fno-strict-aliasing -Wno-long-long -Wno-pointer-sign \
      -Wdeclaration-after-statement
CFLAGS=-g $(COPT) $(CWARN) -fno-strict-aliasing -fno-stack-protector -fpic
CXX=g++
CXXOPT=-O2
CXXFLAGS=$(CXXOPT) -g -Wall -fno-exceptions # -Wvla
LD=g++
LDFLAGS=

VG_CXXFLAGS=-fno-rtti -fno-stack-protector
VG_DEFINES=-DVGA_$(ARCH)=1 -DVGO_$(OS)=1 -DVGP_$(ARCH_OS)=1 -D_STLP_NO_IOSTREAMS=1 -DTS_VALGRIND=1
VG_INCLUDES=-I$(VALGRIND_ROOT) -I$(VALGRIND_ROOT)/include -I$(VALGRIND_ROOT)/VEX/pub -I$(STLPORT_ROOT)

PIN_CXXFLAGS_L=-fPIC
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

# prefix of binary files
P=bin/$(ARCHOS)$(D)-
VGP=$(P)vg-
PINP=$(P)pin-


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
  EXE=
  O=-o
  LINKO=-o
  PIN_CXXFLAGS=$(PIN_CXXFLAGS_L)
  GTEST_LIB=$(GTEST_ROOT)/make/$(ARCHOS)-gtest_main.a
  VG_LD_FLAGS=-static -Wl,-defsym,valt_load_address=0x38000000 -nodefaultlibs -nostartfiles \
    -u _start  -Wl,-T,$(VALGRIND_ROOT)/valt_load_address_$(ARCH_OS).lds
  VG_LD_PRELOAD_FLAGS= -nodefaultlibs -shared -Wl,-z,interpose,-z,initfirst
else ifeq ($(OS), darwin)
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-Wl,-o,
  GTEST_LIB=$(GTEST_ROOT)/make/$(ARCHOS)-gtest_main.a
  VG_LD_FLAGS=-Wl,-u,__start -Wl,-e,__start -Wl,-bind_at_load /usr/lib/dyld -arch i386 \
      -Wl,-seg1addr,0xf0080000 -Wl,-stack_addr,0xf0080000 -Wl,-stack_size,0x80000 -Wl,-pagezero_size,0xf0000000 \
      -nodefaultlibs -nostartfiles
  VG_LD_PRELOAD_FLAGS=-arch i386 -dynamic -dynamiclib -all_load
  VG_CXXFLAGS+=-fno-weak -bind_at_load
else ifeq ($(OS), windows)
  SO=dll
  OBJ=obj
  EXE=.exe
  O=/Fo
  LINKO=/OUT:
  VALGRIND_ROOT= # no valgrind on windows :(
  ARCHFLAGS=
  CXX=cl
  LD=link
  CXXFLAGS=/c /MT /EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /O2
  PIN_DEFINES=/DTARGET_WINDOWS /DBIGARRAY_MULTIPLIER=1 /DUSING_XED /DTARGET_IA32 /DHOST_IA32a
  PIN_INCLUDES=/I$(PIN_ROOT)/source/include /I$(PIN_ROOT)/source/include/gen   /I$(PIN_ROOT)/extras/xed2-ia32/include
  PIN_LDFLAGS=/DLL /EXPORT:main /NODEFAULTLIB  /NOLOGO /INCREMENTAL:NO  /OPT:REF  /MACHINE:x86 /ENTRY:Ptrace_DllMainCRTStartup@12 /BASE:0x55000000
  PIN_LIBPATHS= /LIBPATH:$(PIN_ROOT)/ia32/lib /LIBPATH:$(PIN_ROOT)/ia32/lib-ext  /LIBPATH:$(PIN_ROOT)/extras/xed2-ia32/lib
  PIN_LIBS=pin.lib libxed.lib libcpmt.lib libcmt.lib pinvm.lib kernel32.lib ntdll-32.lib
  GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/Release/gtest.lib
else
  OS=UNKNOWN_OS
endif



VALGRIND_LIBS=$(VALGRIND_ROOT)/coregrind/libcoregrind-$(ARCHOS).a \
		  $(VALGRIND_ROOT)/VEX/libvex-$(ARCHOS).a


dummy:
	@echo "For 64-bit Linux build: make l64"
	@echo "For 32-bit Linux build: make l32"
	@echo "For mixed  Linux build: make l"

all: TS_valgrind TS_pin TS_offline test

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

w: w32
w32: w32d

m: m32
m32: m32o

w32d:
	$(MAKE) all OS=windows ARCH=x86 DEBUG=1

m32o:
	$(MAKE) all OS=darwin ARCH=x86 DEBUG=0

install:
	cp bin/tsan* bin/vgpreload_tsan* $(VALGRIND_INST_ROOT)/lib/valgrind


ifeq ($(VALGRIND_ROOT), )
TS_valgrind:
	@echo VALGRIND_ROOT is not set. Not building the Valgrind-based variant.
else
TS_valgrind:  bin bin/tsan$D-$(ARCHOS) bin/vgpreload_tsan$D-$(ARCHOS).so
endif

ifeq ($(PIN_ROOT), )
TS_pin:
	@echo PIN_ROOT is not set. Not building the PIN-based variant.
else
TS_pin: bin $(P)ts_pin.$(SO)
endif

ifeq ($(OFFLINE), 1)
TS_offline: bin $(P)ts_offline$(EXE)
else
TS_offline:
endif

ifeq ($(GTEST_ROOT), )
test:
	@echo GTEST_ROOT is not set. Not building GTEST-based tests.
else
test: bin $(P)suppressions_test$(EXE)
endif

bin:
	mkdir -p bin

TS_HEADERS=thread_sanitizer.h ts_util.h suppressions.h

TS_VG_OBJECTS=$(VGP)thread_sanitizer.o $(VGP)ts_valgrind.o $(VGP)ts_util.o
TS_VG_HEADERS=ts_valgrind.h ts_valgrind_client_requests.h ts_wrap_and_ignore.h

TS_PIN_OBJECTS=$(PINP)ts_pin.$(OBJ) $(PINP)ts_util.$(OBJ) $(PINP)thread_sanitizer.$(OBJ)

$(P)%.$(OBJ): %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) $(O)$@ -c $< $(DEFINES)


$(VGP)%.o: %.cc $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CXX) $(CXXFLAGS) $(VG_CXXFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VG_DEFINES) -o $@ -c $< $(DEFINES)

$(PINP)%.$(OBJ): %.cc $(TS_HEADERS) $(TS_PIN_HEADERS)
	$(CXX) $(CXXFLAGS) $(PIN_CXXFLAGS) $(ARCHFLAGS) $(PIN_INCLUDES) $(PIN_DEFINES) $(O)$@ -c $< $(DEFINES)

$(P)gtest-%.$(OBJ): %.cc $(TS_HEADERS)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -I$(GTEST_ROOT)/include $(O)$@ -c $<

$(P)preload-%.o: %.c $(TS_HEADERS) $(TS_VG_HEADERS)
	$(CC) $(CFLAGS) $(ARCHFLAGS) $(VG_INCLUDES) $(VG_DEFINES) -o $@ -c $<

bin/tsan$(D)-$(ARCHOS): $(TS_VG_OBJECTS)
	$(LD) $(LDFLAGS) $(ARCHFLAGS) $(VG_LD_FLAGS) -o $@ $^ $(VALGRIND_LIBS) -lgcc

bin/vgpreload_tsan$(D)-$(ARCHOS).so: $(P)preload-ts_valgrind_intercepts.o
	$(LD) $(LDFLAGS)  $(ARCHFLAGS) $(VG_LD_PRELOAD_FLAGS) -o $@  $<

$(P)ts_offline$(EXE): $(P)ts_offline.$(OBJ) $(P)thread_sanitizer.$(OBJ) $(P)ts_util.$(OBJ)
	$(LD) $(LDFLAGS) $(ARCHFLAGS) $(LINKO)$@ $^

$(P)suppressions_test$(EXE): $(P)gtest-suppressions_test.$(OBJ) $(P)suppressions.$(OBJ) $(P)ts_util.$(OBJ) $(GTEST_LIB)
	$(LD) $(LDFLAGS) $(ARCHFLAGS) $(LINKO)$@ $^

$(GTEST_ROOT)/make/$(ARCHOS)-gtest_main.a:
	cd $(GTEST_ROOT)/make && $(MAKE) clean && $(MAKE) CXXFLAGS=$(ARCHFLAGS) && mv gtest_main.a $(ARCHOS)-gtest_main.a

$(P)ts_pin.so: $(TS_PIN_OBJECTS)
	$(LD) $(ARCHFLAGS) $(PIN_LDFLAGS) $(PIN_LIBPATHS) -o $@ $^  $(PIN_LIBS)

$(P)ts_pin.dll: $(TS_PIN_OBJECTS)
	$(LD) $(ARCHFLAGS) $(PIN_LDFLAGS) $(PIN_LIBPATHS)  /IMPLIB:$(PINP)ts_pin.lib /PDB:$(PINP)ts_pin.pdb /OUT:$@  $^  $(PIN_LIBS)

clean:
	rm -rfv bin core* pintool.log* pin.log
