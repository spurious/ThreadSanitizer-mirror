include ../Makefile.inc

INCLUDES=-I../
CXXFLAGS+=$(INCLUDES)

all: libtsan.a

LIBTSAN_HEADERS=tsan_interface.h \
                tsan_atomic.h \
                tsan_compiler.h \
                tsan_linux.h \
                tsan_rtl.h \
                tsan_tread.h

LIBTSAN_OBJ=tsan_rtl.o \
            tsan_linux.o \
            tsan_interceptors.o \
            tsan_thread.o \
            interception_linux.o

%.o: %.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

interception_linux.o: ../interception/interception_linux.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

clean:
	rm -f *.o *.a
