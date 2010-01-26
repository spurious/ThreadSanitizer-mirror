SVN_ROOT=..
include ../common.mk

CFLAGS=$(OPTFLAGS) $(ARCHFLAGS) $(EXTRA_CFLAGS)
CXXFLAGS=$(OPTFLAGS) $(ARCHFLAGS) $(EXTRA_CXXFLAGS)
LDFLAGS=$(LDOPT) $(ARCHFLAGS) $(STATICFLAGS)

# OS = linux, darwin, windows
# ARCH= x86, amd64
# OX = O0, O1, ...
# S= <empty>, -static
BUILD=${OS}-${ARCH}-${OX}${S}
BIN=bin/

ifeq ($(OS), linux)
  EXTRA_CXXFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function 
  EXTRA_CFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function -std=c89 
  LIBS=-lpthread
else ifeq ($(OS), darwin)
  EXTRA_CXXFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function 
  EXTRA_CFLAGS=-Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function -std=c89 
  LIBS=-lpthread
else ifeq ($(OS), windows)
  EXTRA_CXXFLAGS=/EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /D "WIN32"
  EXTRA_CFLAGS=/EHs- /EHa- /wd4530   /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /D "WIN32"
else
  OS=UNKNOWN_OS
endif

all: $(BIN)racecheck_unittest-$(BUILD)$(EXE) $(BIN)demo_tests-$(BUILD)$(EXE) \
     $(BIN)bigtest-$(BUILD)$(EXE) $(BIN)output_test1-$(BUILD)$(EXE)

w:
	$(MAKE) all OS=windows ARCH=x86
l: l32 l64
l32:
	$(MAKE) all OS=linux ARCH=x86
l64:
	$(MAKE) all OS=linux ARCH=amd64

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

ifeq ($(OS), windows)
$(BIN)racecheck_unittest-$(BUILD)$(EXE): $(BIN)windows_tests-$(BUILD).$(OBJ)
else
$(BIN)racecheck_unittest-$(BUILD)$(EXE): $(BIN)posix_tests-$(BUILD).$(OBJ)
endif

$(BIN)demo_tests-$(BUILD)$(EXE): $(BIN)demo_tests-$(BUILD).$(OBJ) \
		$(GTEST_LIB) ${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

$(BIN)output_test1-$(BUILD)$(EXE): $(BIN)output_test1-$(BUILD).$(OBJ) \
		${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

$(BIN)bigtest-$(BUILD)$(EXE): $(BIN)bigtest-$(BUILD).$(OBJ) ${DYNAMIC_ANNOTATIONS_O} | bin
	${LD} $(LDFLAGS) $^ $(LIBS) $(LINKO)$@

$(BIN)%-$(BUILD).$(OBJ): %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h | bin
	${CXX} ${CXXFLAGS} \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -I${GTEST_ROOT}/include \
		 -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c $< $(O)$@

${DYNAMIC_ANNOTATIONS_O}: ${DYNAMIC_ANNOTATIONS_C}
	${CC} ${CFLAGS} $< \
		-I${DYNAMIC_ANNOTATIONS_DIR} -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
		-c $(O)$@

clean: GTEST_CLEAN
	rm -rf bin racecheck_unittest demo_tests bigtest *.o *.O1 *.O2 *.obj *.dSYM *.exe *.pdb *.ilk *.idb *.manifest
