CXX=g++
CXXFLAGS=-g -O2

LIBTSAN=../libtsan.a
GTEST_ROOT=../third_party/googletest
GTEST_INCLUDE=-I$(GTEST_ROOT)/include
GTEST_BUILD_DIR=$(GTEST_ROOT)/build
GTEST_LIB=$(GTEST_BUILD_DIR)/gtest-all.o

INCLUDES=-I.. $(GTEST_INCLUDE)


tsan_test: tsan_test.cc $(LIBTSAN) $(GTEST_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(LIBTSAN) $(GTEST_LIB) -o $@

$(GTEST_LIB):
	mkdir -p $(GTEST_BUILD_DIR) && \
	cd $(GTEST_BUILD_DIR) && \
	$(MAKE) -f ../make/Makefile

clean:
	rm -f tsan_test
