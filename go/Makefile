SRC=go_rtl.cc
TSAN_PATH=../tsan

OBJS32=x86-ts_util.o x86-suppressions.o \
       x86-common_util.o x86-ignore.o \
       x86-thread_sanitizer.o x86-ts_atomic.o
OBJS64=amd64-ts_util.o amd64-suppressions.o \
       amd64-common_util.o amd64-ignore.o \
       amd64-thread_sanitizer.o amd64-ts_atomic.o

FLAGS=-I$(TSAN_PATH) -I../dynamic_annotations \
      -fno-exceptions -fPIC \
      -DTS_GO -Wall

ifeq ($(DEBUG), 1)
  FLAGS+=-fno-inline -g -D_DEBUG
else
  FLAGS+=-O3 -DNDEBUG
endif

FOREIGN_HEADERS=$(TSAN_PATH)/ts_lock.h $(TSAN_PATH)/ts_stats.h \
                $(TSAN_PATH)/ts_heap_info.h $(TSAN_PATH)/ts_trace_info.h \
                $(TSAN_PATH)/ts_simple_cache.h $(TSAN_PATH)/ts_replace.h \
                $(TSAN_PATH)/ts_util.h $(TSAN_PATH)/ts_event_names.h \
                $(TSAN_PATH)/ts_events.h $(TSAN_PATH)/suppressions.h \
                $(TSAN_PATH)/ignore.h $(TSAN_PATH)/common_util.h \
                $(TSAN_PATH)/thread_sanitizer.h \
		$(TSAN_PATH)/ts_atomic.h \
		$(TSAN_PATH)/ts_atomic_int.h

RTL_32_O=tsan_go32.o
RTL_64_O=tsan_go64.o
RTL_32_SO=libtsan_go32.so
RTL_64_SO=libtsan_go64.so

all:	$(RTL_32_SO) $(RTL_64_SO)

$(RTL_32_SO):	$(OBJS32) $(RTL_32_O)
	$(CXX) -o $(RTL_32_SO) $(OBJS32) -m32 -shared $(RTL_32_O)

$(RTL_64_SO):	$(OBJS64) $(RTL_64_O)
	$(CXX) -o $(RTL_64_SO) $(OBJS64) -m64 -shared $(RTL_64_O)

$(TSAN_PATH)/ts_event_names.h:
	make -C $(TSAN_PATH) ts_event_names.h

$(RTL_32_O): $(SRC)
	$(CXX) $(SRC) $(FLAGS) -m32 -c -o $(RTL_32_O)

$(RTL_64_O): $(SRC)
	$(CXX) $(SRC) $(FLAGS) -m64 -c -o $(RTL_64_O)

x86-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(FLAGS)

amd64-%.o:	$(TSAN_PATH)/%.cc $(TS_HEADERS) $(FOREIGN_HEADERS)
	$(CXX) -m64 -o $@ -c $< $(FLAGS)

clean:
	rm -f *.o *.so

