
TOOLNAME=pin_instrument_test
PIN=../pin
CXX=g++
#INLINE=
#INLINE=-fno-inline
INLINE=
OPTLEVEL=-O3
DEBUG=1
CXXFLAGS=-Wall -DDEBUG=$(DEBUG) -Werror -Wno-unknown-pragmas -g $(OPTLEVEL) $(INLINE) -fno-omit-frame-pointer -DBIGARRAY_MULTIPLIER=1 -DUSING_XED -g -fno-strict-aliasing -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX
INCLUDES=-I$(PIN)/extras/xed2-intel64/include -I$(PIN)/source/include -I$(PIN)/source/include/gen
LDFLAGS=-g -shared -Wl,-Bsymbolic -Wl,--version-script=$(PIN)/source/include/pintool.ver
LIBPATHS=-L$(PIN)/extras/xed2-intel64/lib -L$(PIN)/intel64/lib -L$(PIN)/intel64/lib-ext
LIBS= -lpin  -lxed -ldwarf -lelf -ldl

$(TOOLNAME).so: $(TOOLNAME).o
	$(CXX) $(LDFLAGS) $(LIBPATHS) -lpthread -o $(TOOLNAME).so $(TOOLNAME).o  $(LIBS)
$(TOOLNAME).o: $(TOOLNAME).cc Makefile ../common/drd_benchmark_simple.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TOOLNAME).o -c $(TOOLNAME).cc


clean:
	rm -fv *.so *.o core*

run: $(TOOLNAME).so
	$(PIN)/pin -t $(TOOLNAME).so -- $(RUN_TARGET)
