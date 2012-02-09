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

TEST_OBJ=tsan_test.o \
	 tsan_test_util_linux.o

all: tsan_test tsan_c_test


%.o: %.cc $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

%.o: %.c $(LIBTSAN_HEADERS)
	$(CC) $(CXXFLAGS) $(INCLUDES) -c $<

tsan_test: $(TEST_OBJ) $(LIBTSAN) $(GTEST_LIB)
	$(CXX) $(TEST_OBJ) $(LIBTSAN) $(GTEST_LIB) -o $@ $(LINKFLAGS)

tsan_c_test: tsan_c_test.o $(LIBTSAN)
	$(CXX) $< -o $@ $(LIBTSAN) -o $@ $(LINKFLAGS)

$(GTEST_LIB):
	mkdir -p $(GTEST_BUILD_DIR) && \
	cd $(GTEST_BUILD_DIR) && \
	$(MAKE) -f ../make/Makefile CXXFLAGS="$(CXXFLAGS)"

clean:
	rm -f *.o tsan_test 
