include ../Makefile.inc

INCLUDES= -I../ -I../third_party/stlport
EXTRA_CXXFLAGS=-fno-exceptions
NO_SYSROOT=--sysroot=.
CXXFLAGS+=$(EXTRA_CXXFLAGS)
ifeq ($(DEBUG), 0)
  CXXFLAGS+=-fomit-frame-pointer
endif
ifeq ($(CXX), clang++)
  # Global constructors are banned.
  CXXFLAGS+=-Wglobal-constructors
endif


all: libtsan.a

LIBTSAN_HEADERS=tsan_atomic.h \
                tsan_clock.h \
                tsan_compiler.h \
                tsan_defs.h \
		tsan_interface.h \
		tsan_interface_ann.h \
		tsan_interface_inl.h \
                tsan_platform.h \
                tsan_mutex.h \
                tsan_report.h \
		tsan_placement_new.h \
                tsan_rtl.h \
		tsan_slab.h \
		tsan_suppressions.h \
		tsan_symbolize.h \
		tsan_sync.h \
		tsan_trace.h \
		tsan_thread.h

LIBTSAN_OBJ=tsan_clock.o \
            tsan_mutex.o \
            tsan_interceptors.o \
	    tsan_platform_linux.o \
            tsan_report.o \
            tsan_rtl.o \
            tsan_rtl_mutex.o \
            tsan_rtl_thread.o \
            tsan_slab.o \
            tsan_suppressions.o \
            tsan_sync.o \
            tsan_trace.o \
            tsan_thread.o \
            interception_linux.o

ifneq ($(TSAN_SYMB_NULL), )
  LIBTSAN_OBJ+=tsan_symbolize_null.o
else ifneq ($(TSAN_SYMB_LLDB), )
  LIBTSAN_OBJ+=tsan_symbolize_lldb_linux.o
else
  LIBTSAN_OBJ+=tsan_symbolize_addr2line_linux.o
endif

%_linux.o: %_linux.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

%.o: %.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(NO_SYSROOT) -c $<

%.o: ../interception/%.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ 

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

clean:
	rm -f *.o *.a
