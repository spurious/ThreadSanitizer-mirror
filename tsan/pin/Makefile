
#PIN_ROOT is taken from env var.
PIN_BIN=${PIN_ROOT}/pin
CXX=g++

INLINE=
OPTLEVEL=-O3
CXXFLAGS=-DTS_NO_VALGRIND=1 -DTS_USE_STANDARD_STL=1 -Wall -Werror -Wno-unknown-pragmas -g $(OPTLEVEL) $(INLINE) -fno-omit-frame-pointer -DBIGARRAY_MULTIPLIER=1 -DUSING_XED -g -fno-strict-aliasing -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX
INCLUDES=-I..  -I$(PIN_ROOT)/extras/xed2-intel64/include -I$(PIN_ROOT)/source/include -I$(PIN_ROOT)/source/include/gen
LDFLAGS=-g -shared -Wl,-Bsymbolic -Wl,--version-script=$(PIN_ROOT)/source/include/pintool.ver
LIBPATHS=-L$(PIN_ROOT)/extras/xed2-intel64/lib -L$(PIN_ROOT)/intel64/lib -L$(PIN_ROOT)/intel64/lib-ext
LIBS= -lpin  -lxed -ldwarf -lelf -ldl



all: ts_pin_deb.so ts_pin_opt.so
ts_pin_deb.so: ts_pin_deb.o thread_sanitizer_deb.o
ts_pin_opt.so: ts_pin_opt.o

ts_pin_deb.o: ts_pin.cc ../thread_sanitizer.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $< -DDEBUG=1
thread_sanitizer_deb.o: ../thread_sanitizer.cc ../thread_sanitizer.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $< -DDEBUG=1

ts_pin_deb.so: ts_pin_deb.o thread_sanitizer_deb.o
	$(CXX) $(LDFLAGS) $(LIBPATHS) -lpthread -o $@ $^  $(LIBS)
# shortcut.
deb: ts_pin_deb.so

ts_pin_opt.o: ts_pin.cc ../thread_sanitizer.h ../thread_sanitizer.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $< -DDEBUG=0 -DINCLUDE_THREAD_SANITIZER_CC=1



%.so: %.o
	$(CXX) $(LDFLAGS) $(LIBPATHS) -lpthread -o $@ $<  $(LIBS)
%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<

all_tests: simple_pin_test.so


clean:
	        rm -fv *.so *.o core*
