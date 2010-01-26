GTEST_ROOT=../third_party/googletest
CXX=g++
CC=gcc
LD=g++

SUFFIX=
COPT=
CXXOPT=
ARCHFLAGS=
STATICFLAGS=
CFLAGS=$(COPT) $(ARCHFLAGS) $(EXTRA_CFLAGS)
CXXFLAGS=$(CXXOPT) $(ARCHFLAGS) $(EXTRA_CXXFLAGS)
LDFLAGS=$(LDOPT) $(ARCHFLAGS) $(STATICFLAGS)
OS=   # linux, darwin
ARCH= # x86, amd64
OX=    # O0, O1, ...
S=    # <empty>, -static
BUILD=${OS}-${ARCH}-${OX}${S}
BIN=bin/

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
  EXTRA_CXXFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function 
  EXTRA_CFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function -std=c89 
  LIBS=-lpthread
else ifeq ($(OS), darwin)
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-Wl,-o,
  EXTRA_CXXFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function 
  EXTRA_CFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function -std=c89 
  LIBS=-lpthread
else ifeq ($(OS), windows)
  SO=dll
  OBJ=obj
  EXE=.exe
  O=/Fo
  LINKO=/OUT:
  ARCHFLAGS=
  CXX=cl
  CC=cl
  LD=link
  LDOPT=/DEBUG
  EXTRA_CXXFLAGS=/EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /D "WIN32"
  EXTRA_CFLAGS=/EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /D "WIN32"
else
  OS=UNKNOWN_OS
endif

ifeq ($(OS), windows)
  ifeq ($(OPT), 1)
    OX=O1
    COPT=/Zi /MT
    CXXOPT=/Zi /MT
	GTEST_BUILD=release
    GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/Release/gtest.lib
  else
    OX=O0
    COPT=/Od /Zi /MTd
    CXXOPT=/Od /Zi /MTd
	GTEST_BUILD=debug
    GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/Debug/gtestd.lib
  endif
else
  ifeq ($(OPT), 1)
    OX=O1
    COPT=-g -O1
	CXXOPT=-g -O1
  else
    OX=O0
    COPT=-g -O0
	CXXOPT=-g -O0
  endif
  GTEST_LIB=$(GTEST_ROOT)/make/$(BUILD)-gtest_main.a
endif

all: $(BIN)racecheck_unittest-$(BUILD)$(EXE) $(BIN)demo_tests-$(BUILD)$(EXE) \
     $(BIN)bigtest-$(BUILD)$(EXE) $(BIN)output_test1-$(BUILD)$(EXE)

w:
	$(MAKE) OS=windows ARCH=x86
l: l32 l64
l32:
	$(MAKE) OS=linux ARCH=x86
l32:
	$(MAKE) OS=linux ARCH=amd64

bin:
	mkdir -p bin

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_C=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.c
DYNAMIC_ANNOTATIONS_O=$(BIN)dynamic_annotations-$(BUILD).$(OBJ)
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_C} ${DYNAMIC_ANNOTATIONS_H}

$(BIN)racecheck_unittest-$(BUILD)$(EXE): $(BIN)racecheck_unittest-$(BUILD).$(OBJ) $(BIN)old_test_suite-$(BUILD).$(OBJ) \
		$(GTEST_LIB)  ${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} ${LDFLAGS} $^ $(LIBS) $(LINKO)$@

$(BIN)demo_tests-$(BUILD)$(EXE): $(BIN)demo_tests-$(BUILD).$(OBJ) \
		$(GTEST_LIB) ${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

$(BIN)output_test1-$(BUILD)$(EXE): $(BIN)output_test1-$(BUILD).$(OBJ) \
		${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

$(BIN)bigtest-$(BUILD)$(EXE): $(BIN)bigtest-$(BUILD).$(OBJ) ${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

ifeq ($(OS), windows)
$(BIN)racecheck_unittest-$(BUILD)$(EXE): $(BIN)windows_tests-$(BUILD).$(OBJ)
$(GTEST_LIB):
	cd $(GTEST_ROOT)/msvc && devenv /upgrade gtest.sln && devenv /build $(GTEST_BUILD) /project gtest gtest.sln
else
$(BIN)racecheck_unittest-$(BUILD)$(EXE): $(BIN)posix_tests-$(BUILD).$(OBJ)
$(GTEST_LIB):
	cd $(GTEST_ROOT)/make && $(MAKE) clean && $(MAKE) CXXFLAGS="$(CXXFLAGS)" && mv gtest_main.a $(BUILD)-gtest_main.a
endif

$(BIN)%-$(BUILD).$(OBJ): %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h | bin
	${CXX} ${CXXFLAGS} \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -I${GTEST_ROOT}/include \
		 -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c $< $(O)$@

${DYNAMIC_ANNOTATIONS_O}: ${DYNAMIC_ANNOTATIONS_C}
	${CC} ${CFLAGS} $< \
		-I${DYNAMIC_ANNOTATIONS_DIR} -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
		-c $(O)$@

clean:
	rm -rf bin racecheck_unittest demo_tests bigtest *.o *.O1 *.O2 *.obj *.dSYM *.exe *.pdb *.ilk *.idb *.manifest
	cd ${GTEST_ROOT}/make && rm -f *-gtest_main.a && make clean
	rm -f ${GTEST_ROOT}/msvc/*-gtest_main.lib
	rm -rf ${GTEST_ROOT}/msvc/Debug
	rm -rf ${GTEST_ROOT}/msvc/Release
