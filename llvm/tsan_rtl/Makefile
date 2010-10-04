CXX=g++
AR=ar cru
RANLIB=ranlib
SRC=tsan_rtl.cc
ERRORLVL=-Werror
ERRORLVL=-Werror -Wno-deprecated
OBJS32=x86-thread_sanitizer.o x86-ts_util.o x86-suppressions.o
FLAGS=-Istlport -I../../third_party/stlport -DTS_LLVM -fno-exceptions -g -D_STLP_NO_IOSTREAMS=1

TS_HEADERS=thread_sanitizer.h ts_util.h suppressions.h ts_replace.h ts_heap_info.h \
           ts_simple_cache.h ts_stats.h ts_lock.h ts_events.h ts_event_names.h \
           ts_trace_info.h #ts_literace.h ts_race_verifier.h

RTL_32_O=tsan_rtl32.o

all:	$(OBJS32) $(RTL_32_O)
	$(AR) tsan_rtl32.a $(OBJS32) $(RTL_32_O)
	$(RANLIB) tsan_rtl32.a

tsan_rtl32.o: $(SRC) tsan_rtl.h
	$(CXX) $(SRC) $(FLAGS) -m32 -g $(ERRORLVL) -Wall -c -o $(RTL_32_O)

x86-%.o:	%.cc $(TS_HEADERS)
	$(CXX) -m32 -o $@ -c $< $(ERRORLVL) $(FLAGS)

clean:
	rm *.o *.a
