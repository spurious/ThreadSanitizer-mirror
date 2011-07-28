CXX=g++
CC=gcc
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated -Wno-unused-function
TSAN_PATH=../tsan
BFDS_PATH=../bfd_symbolizer
ifneq ($(NO_BFD), 1)
DEFINES32=-DTSAN_RTL_X86 -I$(BFDS_PATH)/binutils32/bin/include
DEFINES64=-DTSAN_RTL_X64 -I$(BFDS_PATH)/binutils64/bin/include
else
DEFINES32=-DTSAN_RTL_X86 -DTSAN_NO_BFD
DEFINES64=-DTSAN_RTL_X64 -DTSAN_NO_BFD
endif
RTL_DEPS=

OBJS32=x86-ts_util.o x86-suppressions.o \
       x86-common_util.o x86-ignore.o x86-tsan_rtl_dynamic_annotations.o \
       x86-thread_sanitizer.o x86-ts_atomic.o x86-dynamic_annotations.o \
       x86-earthquake_wrap.o x86-earthquake_core.o
OBJS64=amd64-ts_util.o amd64-suppressions.o \
       amd64-common_util.o amd64-ignore.o \
       amd64-tsan_rtl_dynamic_annotations.o \
       amd64-thread_sanitizer.o amd64-ts_atomic.o amd64-dynamic_annotations.o \
       amd64-earthquake_wrap.o amd64-earthquake_core.o

COMMON_FLAGS=-I. -I../third_party/stlport \
      -I$(TSAN_PATH) -I../dynamic_annotations \
      -I../earthquake \
      -fno-exceptions -g -O2 \
      -D_STLP_NO_IOSTREAMS=1 -DTS_LLVM -fPIE

DA_FLAGS=$(COMMON_FLAGS) -DDYNAMIC_ANNOTATIONS_PROVIDE_RUNNING_ON_VALGRIND=0

ifeq ($(DEBUG), 1)
  D=-debug
  DEFINES32+=-DDEBUG=1
  DEFINES64+=-DDEBUG=1
  DA_FLAGS+=-DDYNAMIC_ANNOTATIONS_ENABLED=1
  COMMON_FLAGS+=-fno-inline
else
  D=
endif

ifeq ($(TS_DIRECT_MAP), 1)
  COMMON_FLAGS+=-DTS_DIRECT_MAP=1
endif

FLAGS=$(COMMON_FLAGS)

FOREIGN_HEADERS=$(TSAN_PATH)/ts_lock.h $(TSAN_PATH)/ts_stats.h \
                $(TSAN_PATH)/ts_heap_info.h $(TSAN_PATH)/ts_trace_info.h \
                $(TSAN_PATH)/ts_simple_cache.h $(TSAN_PATH)/ts_replace.h \
                $(TSAN_PATH)/ts_util.h $(TSAN_PATH)/ts_event_names.h \
                $(TSAN_PATH)/ts_events.h $(TSAN_PATH)/suppressions.h \
                $(TSAN_PATH)/ignore.h $(TSAN_PATH)/common_util.h \
                $(TSAN_PATH)/thread_sanitizer.h \
		$(TSAN_PATH)/ts_atomic.h \
		$(TSAN_PATH)/ts_atomic_int.h

TS_HEADERS=

RTL_32_O=tsan_rtl32.o
RTL_64_O=tsan_rtl64.o
RTL_32_A=lib/tsan_rtl32.a
RTL_64_A=lib/tsan_rtl64.a

all:	$(RTL_32_A) $(RTL_64_A)

$(RTL_32_A):	$(OBJS32) $(RTL_32_O)
ifneq ($(NO_BFD), 1)
	rm -Rf bfds32 && mkdir bfds32 && cd bfds32 && ar x ../$(BFDS_PATH)/bfds32.a
	$(AR) $(RTL_32_A) $(OBJS32) $(RTL_32_O) ./bfds32/*.o
else
	$(AR) $(RTL_32_A) $(OBJS32) $(RTL_32_O)
endif
	$(RANLIB) $(RTL_32_A)

$(RTL_64_A):	$(OBJS64) $(RTL_64_O)
ifneq ($(NO_BFD), 1)
	rm -Rf bfds64 && mkdir bfds64 && cd bfds64 && ar x ../$(BFDS_PATH)/bfds64.a
	$(AR) $(RTL_64_A) $(OBJS64) $(RTL_64_O) ./bfds64/*.o
else
	$(AR) $(RTL_64_A) $(OBJS64) $(RTL_64_O)
endif
	$(RANLIB) $(RTL_64_A)

$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

tsan_rtl32.o: $(SRC) tsan_rtl.h $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m32 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_32_O) $(DEFINES32) -DTSAN_RTL_X86

tsan_rtl64.o: $(SRC) tsan_rtl.h $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m64 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_64_O) $(DEFINES64) -DTSAN_RTL_X64

ifneq ($(NO_BFD), 1)
BFDS_LIBS=$(BFDS_PATH)/bfds32.a $(BFDS_PATH)/bfds64.a
endif

x86-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS) $(BFDS_LIBS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) $(DEFINES32)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)

x86-%.o:	../earthquake/%.c
	$(CC) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)

x86-dynamic_annotations.o:	$(TSAN_PATH)/../dynamic_annotations/dynamic_annotations.c $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CC) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)


amd64-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS) $(BFDS_LIBS)
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(FLAGS) $(DEFINES64)

amd64-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES64)

amd64-%.o:	../earthquake/%.c
	$(CC) -m64 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES64)

amd64-dynamic_annotations.o:	$(TSAN_PATH)/../dynamic_annotations/dynamic_annotations.c $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CC) -m64 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES64)

$(BFDS_LIBS):
	cd $(BFDS_PATH) && make

clean:
	rm -f *.o
	rm -f lib/*.a
	rm -Rf bfds64
	rm -Rf bfds32

# These targets are currently unused.
x86-stack_info.o:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) -USTACKINFO_STANDALONE

stack_info32:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ $< $(ERRORLVL) $(FLAGS) -DSTACKINFO_STANDALONE -lpthread

amd64-stack_info.o:	stack_info.cc stack_info.h
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(FLAGS) -USTACKINFO_STANDALONE

stack_info64:	stack_info.cc stack_info.h
	$(CXX) -m64 -o $@ $< $(ERRORLVL) $(FLAGS) -DSTACKINFO_STANDALONE -lpthread
