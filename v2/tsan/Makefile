include ../Makefile.inc

INCLUDES=-I../

all: libtsan.a

LIBTSAN_HEADERS=tsan_interceptors.h \
                tsan_interface.h \
                tsan_atomic.h \
                tsan_compiler.h \
                tsan_linux.h \
                tsan_rtl.h

LIBTSAN_OBJ=tsan_rtl.o \
            tsan_linux.o \
            tsan_interceptors.o \
            interception_linux.o

%.o: %.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

%.o: ../interception/%.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<


libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

clean:
	rm -f *.o *.a
