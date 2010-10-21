CXX=g++
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated -Wno-unused-function
OBJS32=x86-thread_sanitizer.o x86-ts_util.o x86-suppressions.o
TSAN_PATH=../../tsan
FLAGS=-I. -Istlport -I../../third_party/stlport -I$(TSAN_PATH) \
      -fno-exceptions -g -O0 \
      -D_STLP_NO_IOSTREAMS=1 -DTS_LLVM \
      -DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK

FOREIGN_HEADERS=$(TSAN_PATH)/ts_lock.h $(TSAN_PATH)/ts_stats.h \
                $(TSAN_PATH)/ts_heap_info.h $(TSAN_PATH)/ts_trace_info.h \
                $(TSAN_PATH)/ts_simple_cache.h $(TSAN_PATH)/ts_replace.h \
                $(TSAN_PATH)/ts_util.h $(TSAN_PATH)/ts_event_names.h \
                $(TSAN_PATH)/ts_events.h $(TSAN_PATH)/suppressions.h

TS_HEADERS=thread_sanitizer.h

RTL_32_O=tsan_rtl32.o

all:	$(OBJS32) $(RTL_32_O)
	$(AR) tsan_rtl32.a $(OBJS32) $(RTL_32_O)
	$(RANLIB) tsan_rtl32.a


$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

tsan_rtl32.o: $(SRC) tsan_rtl.h
	$(CXX) $(SRC) $(FLAGS) -m32 -g $(ERRORLVL) -Wall -c -o $(RTL_32_O)

x86-%.o:	%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS)


clean:
	rm -f *.o *.a
