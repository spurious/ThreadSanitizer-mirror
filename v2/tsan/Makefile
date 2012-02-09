include ../Makefile.inc

INCLUDES= -I../

all: libtsan.a

LIBTSAN_HEADERS=tsan_atomic.h \
                tsan_clock.h \
                tsan_compiler.h \
                tsan_defs.h \
		tsan_interface.h \
                tsan_linux.h \
                tsan_mutex.h \
                tsan_report.h \
                tsan_rtl.h \
		tsan_slab.h \
		tsan_suppressions.h \
		tsan_sync.h \
		tsan_trace.h \
		tsan_thread.h

LIBTSAN_OBJ=tsan_clock.o \
            tsan_linux.o \
            tsan_mutex.o \
            tsan_interceptors.o \
            tsan_interface.o \
            tsan_report.o \
            tsan_rtl.o \
            tsan_slab.o \
            tsan_suppressions.o \
            tsan_sync.o \
            tsan_trace.o \
            tsan_thread.o \
            interception_linux.o

%.o: %.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

%.o: ../interception/%.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

clean:
	rm -f *.o *.a
