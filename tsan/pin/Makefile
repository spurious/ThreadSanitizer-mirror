
#PIN_ROOT is taken from env var.
PIN_BIN=${PIN_ROOT}/pin
CXX=g++

ARCH=L64
DEBUG=1

INLINE=
OPTLEVEL=-O3
CXXFLAGS_L=$(OPTLEVEL) $(INLINE) -Wall -Werror -Wno-unknown-pragmas -g -fno-omit-frame-pointer -fno-strict-aliasing -fPIC
CXXFLAGS_L64=-DBIGARRAY_MULTIPLIER=1 -DUSING_XED  -DTARGET_IA32E -DHOST_IA32E  -DTARGET_LINUX
CXXFLAGS_L32=-DBIGARRAY_MULTIPLIER=1 -DUSING_XED  -DTARGET_IA32  -DHOST_IA32   -DTARGET_LINUX
INCLUDES_L64=-I..  -I$(PIN_ROOT)/extras/xed2-intel64/include -I$(PIN_ROOT)/source/include -I$(PIN_ROOT)/source/include/gen
INCLUDES_L32=-I..  -I$(PIN_ROOT)/extras/xed2-ia32/include -I$(PIN_ROOT)/source/include -I$(PIN_ROOT)/source/include/gen
LIBPATHS_L64=-L$(PIN_ROOT)/extras/xed2-intel64/lib -L$(PIN_ROOT)/intel64/lib -L$(PIN_ROOT)/intel64/lib-ext
LIBPATHS_L32=-L$(PIN_ROOT)/extras/xed2-ia32/lib -L$(PIN_ROOT)/ia32/lib -L$(PIN_ROOT)/ia32/lib-ext
LDFLAGS_L=-g -shared -Wl,-Bsymbolic -Wl,--version-script=$(PIN_ROOT)/source/include/pintool.ver
LIBS_L= -lpin  -lxed -ldwarf -lelf -ldl -lpthread

ifeq ($(ARCH), L64)
  CXXFLAGS=$(CXXFLAGS_L) $(CXXFLAGS_L64) $(INCLUDES_L64) -m64
  LDFLAGS=$(LDFLAGS_L) $(LIBPATHS_L64) -m64
  LIBS=$(LIBS_L)
  ARCH_SUFIX=_l64
else ifeq ($(ARCH), L32)
  CXXFLAGS=$(CXXFLAGS_L) $(CXXFLAGS_L32) $(INCLUDES_L32) -m32
  LDFLAGS=$(LDFLAGS_L) $(LIBPATHS_L32) -m32
  LIBS=$(LIBS_L)
  ARCH_SUFIX=_l32
else

endif

ifeq ($(DEBUG), 1)
  BUILD_SUFIX=_deb
  DEFINES=-DDEBUG=1
else
  BUILD_SUFIX=_opt
  DEFINES=-DINCLUDE_THREAD_SANITIZER_CC=1
endif

SUFIX=${ARCH_SUFIX}$(BUILD_SUFIX)


all:

pintool: ts_pin$(SUFIX).so

l: l32 l64
l64: l64o l64d
l32: l32o l32d
ld: l64d l32d
lo: l64o l32o

l64d:
	$(MAKE) pintool ARCH=L64  DEBUG=1
l64o:
	$(MAKE) pintool ARCH=L64  DEBUG=0

l32d:
	$(MAKE) pintool ARCH=L32  DEBUG=1
l32o:
	$(MAKE) pintool ARCH=L32  DEBUG=0



ts_pin$(SUFIX).so: ts_pin$(SUFIX).o ts_util$(SUFIX).o thread_sanitizer$(SUFIX).o
	$(CXX) $(LDFLAGS) $(LIBPATHS) -o $@ $^  $(LIBS)

HEADERS=../thread_sanitizer.h ../ts_util.h

#all_tests: simple_pin_test.so detach_example.so
#%.so: %.o
#	$(CXX) $(LDFLAGS) $(LIBPATHS) -o $@ $<  $(LIBS)

%$(ARCH_SUFIX)$(BUILD_SUFIX).o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(DEFINES)
%$(ARCH_SUFIX)$(BUILD_SUFIX).o: ../%.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(DEFINES)

clean:
	rm -fv *.so *.o core*
