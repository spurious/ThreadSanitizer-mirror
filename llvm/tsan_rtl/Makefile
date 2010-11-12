CXX=g++
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated -Wno-unused-function
OBJS32=x86-thread_sanitizer.o x86-ts_util.o x86-suppressions.o x86-common_util.o x86-ignore.o
TSAN_PATH=../../tsan
DEFINES=

ifeq ($(DEBUG), 1)
  D=-debug
  DEFINES+=-DDEBUG=1
  OBJS32=x86-thread_sanitizer.o x86-ts_util.o x86-suppressions.o x86-common_util.o x86-ignore.o
else
  D=
  # In optimized mode, thread_sanitizer.cc is included into the tool code.
  DEFINES+=-DINCLUDE_THREAD_SANITIZER_CC=1
  OBJS32=x86-ts_util.o x86-suppressions.o x86-common_util.o x86-ignore.o
endif


FLAGS=-I. -Istlport -I../../third_party/stlport -I$(TSAN_PATH) \
      -fno-exceptions -g -O2 \
      -D_STLP_NO_IOSTREAMS=1 -DTS_LLVM \
      -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK

FOREIGN_HEADERS=$(TSAN_PATH)/ts_lock.h $(TSAN_PATH)/ts_stats.h \
                $(TSAN_PATH)/ts_heap_info.h $(TSAN_PATH)/ts_trace_info.h \
                $(TSAN_PATH)/ts_simple_cache.h $(TSAN_PATH)/ts_replace.h \
                $(TSAN_PATH)/ts_util.h $(TSAN_PATH)/ts_event_names.h \
                $(TSAN_PATH)/ts_events.h $(TSAN_PATH)/suppressions.h \
                $(TSAN_PATH)/ignore.h $(TSAN_PATH)/common_util.h \
                $(TSAN_PATH)/thread_sanitizer.h

TS_HEADERS=

RTL_32_O=tsan_rtl32.o

all:	$(OBJS32) $(RTL_32_O)
	$(AR) tsan_rtl32.a $(OBJS32) $(RTL_32_O)
	$(RANLIB) tsan_rtl32.a


$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

tsan_rtl32.o: $(SRC) tsan_rtl.h
	$(CXX) $(SRC) $(FLAGS) -m32 -g $(ERRORLVL) -Wall -c -o $(RTL_32_O) $(DEFINES)

x86-stack_info.o:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS) -USTACKINFO_STANDALONE

stack_info32:	stack_info.cc stack_info.h
	$(CXX) -m32 -o $@ $< $(ERRORLVL) $(FLAGS) -DSTACKINFO_STANDALONE -lpthread

x86-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS)


clean:
	rm -f *.o *.a
