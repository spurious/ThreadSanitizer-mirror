CXX=g++
CC=gcc
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated -Wno-unused-function
TSAN_PATH=../../tsan
DEFINES=
RTL_DEPS=

OBJS32=x86-ts_util.o x86-suppressions.o \
       x86-common_util.o x86-ignore.o x86-tsan_rtl_dynamic_annotations.o \
       x86-thread_sanitizer.o x86-dynamic_annotations.o
OBJS64=amd64-ts_util.o amd64-suppressions.o \
       amd64-common_util.o amd64-ignore.o \
       amd64-tsan_rtl_dynamic_annotations.o \
       amd64-thread_sanitizer.o amd64-dynamic_annotations.o

COMMON_FLAGS=-I. -Istlport -I../../third_party/stlport \
      -I$(TSAN_PATH) -I../../dynamic_annotations \
      -fno-exceptions -g -O2 \
      -D_STLP_NO_IOSTREAMS=1 -DTS_LLVM


DA_FLAGS=$(COMMON_FLAGS) -DDYNAMIC_ANNOTATIONS_PROVIDE_RUNNING_ON_VALGRIND=0

ifeq ($(DEBUG), 1)
  D=-debug
  DEFINES+=-DDEBUG=1
  DA_FLAGS+=-DDYNAMIC_ANNOTATIONS_ENABLED=1
  COMMON_FLAGS+=-fno-inline
else
  D=
endif


FLAGS=$(COMMON_FLAGS) -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK

FOREIGN_HEADERS=$(TSAN_PATH)/ts_lock.h $(TSAN_PATH)/ts_stats.h \
                $(TSAN_PATH)/ts_heap_info.h $(TSAN_PATH)/ts_trace_info.h \
                $(TSAN_PATH)/ts_simple_cache.h $(TSAN_PATH)/ts_replace.h \
                $(TSAN_PATH)/ts_util.h $(TSAN_PATH)/ts_event_names.h \
                $(TSAN_PATH)/ts_events.h $(TSAN_PATH)/suppressions.h \
                $(TSAN_PATH)/ignore.h $(TSAN_PATH)/common_util.h \
                $(TSAN_PATH)/thread_sanitizer.h

TS_HEADERS=

RTL_32_O=tsan_rtl32.o
RTL_64_O=tsan_rtl64.o
RTL_32_A=tsan_rtl32.a
RTL_64_A=tsan_rtl64.a

all:	$(RTL_32_A) $(RTL_64_A)

$(RTL_32_A):	$(OBJS32) $(RTL_32_O)
	$(AR) tsan_rtl32.a $(OBJS32) $(RTL_32_O)
	$(RANLIB) tsan_rtl32.a

$(RTL_64_A):	$(OBJS64) $(RTL_64_O)
	$(AR) tsan_rtl64.a $(OBJS64) $(RTL_64_O)
	$(RANLIB) tsan_rtl64.a



$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

tsan_rtl32.o: $(SRC) tsan_rtl.h $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m32 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_32_O) $(DEFINES) -DTSAN_RTL_X86

tsan_rtl64.o: $(SRC) tsan_rtl.h $(RTL_DEPS)
	$(CXX) $(SRC) $(FLAGS) -m64 $(ERRORLVL) $(DA_FLAGS) -Wall -c -o $(RTL_64_O) $(DEFINES) -DTSAN_RTL_X64


x86-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) $(DEFINES)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES)

x86-dynamic_annotations.o:	$(TSAN_PATH)/../dynamic_annotations/dynamic_annotations.c $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CC) -m32 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES)


amd64-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(FLAGS) $(DEFINES)

amd64-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES)

amd64-dynamic_annotations.o:	$(TSAN_PATH)/../dynamic_annotations/dynamic_annotations.c $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CC) -m64 -o $@ -c $< $(ERRORLVL) $(DA_FLAGS) $(DEFINES)

clean:
	rm -f *.o *.a

# These targets are currently unused.
x86-stack_info.o:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) -USTACKINFO_STANDALONE

stack_info32:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ $< $(ERRORLVL) $(FLAGS) -DSTACKINFO_STANDALONE -lpthread

amd64-stack_info.o:	stack_info.cc stack_info.h
	$(CXX) -m64 -o $@ -c $< $(ERRORLVL) $(FLAGS) -USTACKINFO_STANDALONE

stack_info64:	stack_info.cc stack_info.h
	$(CXX) -m64 -o $@ $< $(ERRORLVL) $(FLAGS) -DSTACKINFO_STANDALONE -lpthread
