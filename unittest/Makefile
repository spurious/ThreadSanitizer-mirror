GTEST_ROOT=../third_party/googletest
CXX=g++
CC=gcc

SUFFIX=
COPT=
CXXOPT=
ARCHFLAGS=
STATICFLAGS=
CFLAGS=-g $(COPT) $(ARCHFLAGS)
CXXFLAGS=-g $(CXXOPT) $(ARCHFLAGS)
LDFLAGS=$(ARCHFLAGS) $(STATICFLAGS)
OS=   # linux, darwin
ARCH= # x86, amd64
OX=    # O0, O1, ...
S=    # <empty>, -static
BUILD=${OS}-${ARCH}-${OX}${S}
BIN=bin/

ifeq ($(OPT), 1)
  OX=O1
  COPT=-O1
  CXXOPT=-O1
else
  OX=O0
  COPT=-O0
  CXXOPT=-O0
endif

ifeq ($(STATIC), 1)
  S=-static
  STATICFLAGS=-static
else
  S=
  STATICFLAGS=
endif

ifeq ($(ARCH), x86)
  ARCHFLAGS=-m32
else ifeq ($(ARCH), amd64)
  ARCHFLAGS=-m64
else
  ARCH=UNKNOWN_ARCH
endif

ifeq ($(OS), linux)
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-o
  GTEST_LIB=$(GTEST_ROOT)/make/$(BUILD)-gtest_main.a
else ifeq ($(OS), darwin)
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-Wl,-o,
  GTEST_LIB=$(GTEST_ROOT)/make/$(BUILD)-gtest_main.a
else ifeq ($(OS), windows)
  SO=dll
  OBJ=obj
  EXE=.exe
  O=/Fo
  LINKO=/OUT:
  ARCHFLAGS=
  CXX=cl
  LD=link
  CXXFLAGS=/c /MT /EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /O2
  GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/Release/gtest.lib
else
  OS=UNKNOWN_OS
endif

all: $(BIN)racecheck_unittest-$(BUILD) $(BIN)demo_tests-$(BUILD) $(BIN)bigtest-$(BUILD)

bin:
	mkdir -p bin

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_C=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.c
DYNAMIC_ANNOTATIONS_O=$(BIN)dynamic_annotations-$(BUILD).$(OBJ)
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_C} ${DYNAMIC_ANNOTATIONS_H}

$(BIN)racecheck_unittest-$(BUILD): $(BIN)racecheck_unittest-$(BUILD).$(OBJ) $(BIN)old_test_suite-$(BUILD).$(OBJ) \
		$(BIN)posix_tests-$(BUILD).$(OBJ) $(GTEST_LIB)  ${DYNAMIC_ANNOTATIONS_O} | bin
	${CXX} ${LDFLAGS} $^ -lpthread -o $@

$(BIN)demo_tests-$(BUILD): $(BIN)old_test_suite-$(BUILD).$(OBJ) $(BIN)demo_tests-$(BUILD).$(OBJ) \
		$(GTEST_LIB) ${DYNAMIC_ANNOTATIONS_O} | bin
	${CXX} $^ -lpthread -o $@

$(BIN)bigtest-$(BUILD): $(BIN)bigtest-$(BUILD).$(OBJ) ${DYNAMIC_ANNOTATIONS_O} | bin
	${CXX} $^ -lpthread -o $@

$(GTEST_LIB):
	cd $(GTEST_ROOT)/make && $(MAKE) clean && $(MAKE) CXXFLAGS="$(CXXFLAGS)" && mv gtest_main.a $(BUILD)-gtest_main.a

$(BIN)%-$(BUILD).$(OBJ): %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h | bin
	${CXX} ${CXXFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -I${GTEST_ROOT}/include \
		 -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c -o $@

${DYNAMIC_ANNOTATIONS_O}: ${DYNAMIC_ANNOTATIONS_C}
	${CC} ${CFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function \
		-std=c89 -I${DYNAMIC_ANNOTATIONS_DIR} -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
		-c -o $@

# TODO(timurrrr): do smarter dependency list
win: racecheck_unittest.cc old_test_suite.cc windows_tests.cc demo_tests.cc ${DYNAMIC_ANNOTATIONS} \
     thread_wrappers.h thread_wrappers_win.h old_test_suite.h test_utils.h
	cd ${GTEST_ROOT}/msvc && devenv /build debug /project gtest gtest.sln
	cl.exe ${CXXFLAGS} /Od /Zi /EHsc /nologo /MTd \
		/D "DYNAMIC_ANNOTATIONS_ENABLED=1" /D "WIN32" \
		-I${DYNAMIC_ANNOTATIONS_DIR} -I${GTEST_ROOT}/include \
		racecheck_unittest.cc old_test_suite.cc windows_tests.cc \
		${DYNAMIC_ANNOTATIONS_C} ${GTEST_ROOT}/msvc/gtest/Debug/gtestd.lib


clean:
	rm -rf bin racecheck_unittest demo_tests bigtest *.o *.O1 *.O2 *.obj *.dSYM *.exe *.pdb *.ilk *.idb *.manifest
	cd ${GTEST_ROOT}/make && rm -f *-gtest_main.a && make clean
