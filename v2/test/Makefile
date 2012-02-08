CXX=g++
CXXFLAGS=-fPIE -Wall -Werror
LINKFLAGS=-ldl -lpthread -pie

ifeq ($(DEBUG), )
	CXXFLAGS+=-O2 -DNDEBUG
else
	CXXFLAGS+=-g -D_DEBUG
endif

LIBTSAN=../libtsan.a
GTEST_ROOT=../third_party/googletest
GTEST_INCLUDE=-I$(GTEST_ROOT)/include
GTEST_BUILD_DIR=$(GTEST_ROOT)/build
GTEST_LIB=$(GTEST_BUILD_DIR)/gtest-all.o

INCLUDES=-I../llvm $(GTEST_INCLUDE)

TEST_OBJ=tsan_test.o \
	 tsan_test_util_linux.o

%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<

tsan_test: $(TEST_OBJ) $(LIBTSAN) $(GTEST_LIB)
	$(CXX) $(TEST_OBJ) $(LIBTSAN) $(GTEST_LIB) -o $@ $(LINKFLAGS)

$(GTEST_LIB):
	mkdir -p $(GTEST_BUILD_DIR) && \
	cd $(GTEST_BUILD_DIR) && \
	$(MAKE) -f ../make/Makefile CXXFLAGS="$(CXXFLAGS)"

clean:
	rm -f tsan_test *.o
