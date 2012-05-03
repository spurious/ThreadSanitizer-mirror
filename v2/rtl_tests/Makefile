include ../Makefile.inc

LINKFLAGS=-ldl -lpthread -pie

LIBTSAN=../tsan/libtsan.a
GTEST_ROOT=../third_party/googletest
GTEST_INCLUDE=-I$(GTEST_ROOT)/include
GTEST_BUILD_DIR=$(GTEST_ROOT)/build
GTEST_LIB=$(GTEST_BUILD_DIR)/gtest-all.o

INCLUDES=-I../tsan $(GTEST_INCLUDE)

LIBTSAN_HEADERS=tsan_test_util.h \
                ../tsan/tsan_atomic.h \
                ../tsan/tsan_compiler.h

TEST_OBJ=tsan_bench.o \
	 tsan_mop.o \
	 tsan_mutex.o \
	 tsan_posix.o \
	 tsan_string.o \
	 tsan_test.o \
	 tsan_thread.o \
	 tsan_test_util_linux.o

UTEST_SRC=$(wildcard ../unit_tests/*_test.cc)
UTEST_OBJ=$(patsubst %.cc,%.o,$(UTEST_SRC))
UTEST_HDR=$(wildcard ../tsan/*.h)

all: tsan_test

../unit_tests/%_test.o: ../unit_tests/%_test.cc $(UTEST_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<

%.o: %.cc $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

%.o: %.c $(LIBTSAN_HEADERS)
	$(CC) $(CXXFLAGS) $(INCLUDES) -c $<

tsan_test: $(TEST_OBJ) $(UTEST_OBJ) $(LIBTSAN) $(GTEST_LIB)
	$(CXX) $(TEST_OBJ) $(UTEST_OBJ) $(LIBTSAN) $(GTEST_LIB) -o $@ $(LINKFLAGS) $(LDFLAGS)

$(GTEST_LIB):
	mkdir -p $(GTEST_BUILD_DIR) && \
	cd $(GTEST_BUILD_DIR) && \
	$(MAKE) -f ../make/Makefile CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC) CXX=$(CXX)

clean:
	rm -f *.o tsan_test
