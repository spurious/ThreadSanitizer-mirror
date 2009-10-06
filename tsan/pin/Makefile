
#PIN_ROOT is taken from env var.
PIN_BIN=${PIN_ROOT}/pin
CXX=g++

INLINE=
OPTLEVEL=-O3
CXXFLAGS=-Wall -Werror -Wno-unknown-pragmas -g $(OPTLEVEL) $(INLINE) -fno-omit-frame-pointer -DBIGARRAY_MULTIPLIER=1 -DUSING_XED -g -fno-strict-aliasing -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX
INCLUDES=-I$(PIN_ROOT)/extras/xed2-intel64/include -I$(PIN_ROOT)/source/include -I$(PIN_ROOT)/source/include/gen
LDFLAGS=-g -shared -Wl,-Bsymbolic -Wl,--version-script=$(PIN_ROOT)/source/include/pintool.ver
LIBPATHS=-L$(PIN_ROOT)/extras/xed2-intel64/lib -L$(PIN_ROOT)/intel64/lib -L$(PIN_ROOT)/intel64/lib-ext
LIBS= -lpin  -lxed -ldwarf -lelf -ldl

%.so: %.o
	$(CXX) $(LDFLAGS) $(LIBPATHS) -lpthread -o $@ $<  $(LIBS)
%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<

all_tests: simple_pin_test.so


clean:
	        rm -fv *.so *.o core*
