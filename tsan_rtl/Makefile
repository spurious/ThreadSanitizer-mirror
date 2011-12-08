CXX=g++
CC=gcc
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated -Wno-unused-function
TSAN_PATH=../tsan
BFDS_PATH=../bfd_symbolizer
INSTALL_PATH=../clang_build_Linux/Release+Asserts/

ifeq ($(USE_DTLEB), 1)
DTLEB_DEFINES=-DUSE_DYNAMIC_TLEB
else
DTLEB_DEFINES=
endif

DEFINES32=-DTSAN_RTL_X86 $(BFD_DEFINES32) $(DTLEB_DEFINES)
DEFINES64=-DTSAN_RTL_X64 $(BFD_DEFINES64) $(DTLEB_DEFINES)

ifeq ($(USE_SEGV), 1)
DEFINES32+= -DFLUSH_WITH_SEGV
DEFINES64+= -DFLUSH_WITH_SEGV
endif

ifeq ($(NO_DRD), 1)
DEFINES32+= -DDISABLE_RACE_DETECTION
DEFINES64+= -DDISABLE_RACE_DETECTION
endif

RTL_DEPS=tsan_rtl.h tsan_rtl_wrap.h tsan_rtl_symbolize.h

OBJS32=x86-ts_util.o x86-suppressions.o \
       x86-common_util.o x86-ignore.o x86-tsan_rtl_dynamic_annotations.o \
       x86-thread_sanitizer.o x86-ts_atomic.o x86-dynamic_annotations.o \
       x86-earthquake_wrap.o x86-earthquake_core.o x86-tsan_rtl_wrap.o
OBJS64=amd64-ts_util.o amd64-suppressions.o \
       amd64-common_util.o amd64-ignore.o \
       amd64-tsan_rtl_dynamic_annotations.o \
       amd64-thread_sanitizer.o amd64-ts_atomic.o amd64-dynamic_annotations.o \
       amd64-earthquake_wrap.o amd64-earthquake_core.o  amd64-tsan_rtl_wrap.o

ifeq ($(GCC), 1)
DEFINES32+=-I$(BFDS_PATH)/binutils32/bin/include -DGCC
DEFINES64+=-I$(BFDS_PATH)/binutils64/bin/include -DGCC
OBJS32+= x86-tsan_rtl_symbolize_bfd.o
OBJS64+= amd64-tsan_rtl_symbolize_bfd.o
else
OBJS32+= x86-tsan_rtl_symbolize_llvm.o
OBJS64+= amd64-tsan_rtl_symbolize_llvm.o
endif

COMMON_FLAGS=-I. -I../third_party/stlport \
      -I$(TSAN_PATH) -I../dynamic_annotations \
      -I../earthquake -I$(BFDS_PATH) \
      -fno-exceptions -g \
      -D_STLP_NO_IOSTREAMS=1 -DTS_LLVM -fPIE \
      -DDYNAMIC_ANNOTATIONS_PREFIX=LLVM

DA_FLAGS=$(COMMON_FLAGS) -DDYNAMIC_ANNOTATIONS_PROVIDE_RUNNING_ON_VALGRIND=0

ifeq ($(DEBUG), 1)
  DEFINES32+=-DDEBUG=1
  DEFINES64+=-DDEBUG=1
  DA_FLAGS+=-DDYNAMIC_ANNOTATIONS_ENABLED=1
  COMMON_FLAGS+=-fno-inline
else
  COMMON_FLAGS+=-O2
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

install_llvm:	all
	cat scripts/link_config.txt | grep -v "^$$\|#" | sed "s/^/--/" > $(INSTALL_PATH)/lib/link_config.txt
	echo "--wrap __libc_csu_init" >> $(INSTALL_PATH)/lib/link_config.txt
	cp $(RTL_32_A) $(INSTALL_PATH)/lib
	cp $(RTL_64_A) $(INSTALL_PATH)/lib

$(RTL_32_A):	$(OBJS32) $(RTL_32_O)
ifeq ($(GCC), 1)
	rm -Rf bfds32 && mkdir bfds32 && cd bfds32 && ar x ../$(BFDS_PATH)/bfds32.a
	$(AR) $(RTL_32_A) $(OBJS32) $(RTL_32_O) ./bfds32/*.o
else
	$(AR) $(RTL_32_A) $(OBJS32) $(RTL_32_O)
endif
	$(RANLIB) $(RTL_32_A)

$(RTL_64_A):	$(OBJS64) $(RTL_64_O)
ifeq ($(GCC), 1)
	rm -Rf bfds64 && mkdir bfds64 && cd bfds64 && ar x ../$(BFDS_PATH)/bfds64.a
	$(AR) $(RTL_64_A) $(OBJS64) $(RTL_64_O) ./bfds64/*.o
else
	$(AR) $(RTL_64_A) $(OBJS64) $(RTL_64_O)
endif
	$(RANLIB) $(RTL_64_A)

$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

tsan_rtl32.o: $(SRC) $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m32 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_32_O) $(DEFINES32)

tsan_rtl64.o: $(SRC) $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m64 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_64_O) $(DEFINES64)

ifeq ($(GCC), 1)
BFDS_LIBS=$(BFDS_PATH)/bfds32.a $(BFDS_PATH)/bfds64.a
endif

x86-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS) $(BFDS_LIBS) $(RTL_DEPS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) $(DEFINES32)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)

x86-%.o:	../earthquake/%.c
	$(CC) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)

x86-dynamic_annotations.o:	$(TSAN_PATH)/../dynamic_annotations/dynamic_annotations.c $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CC) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES32)


amd64-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS) $(BFDS_LIBS) $(RTL_DEPS)
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
