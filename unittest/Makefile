CXX=g++
CC=gcc

SUFFIX=
CXXFLAGS=

all: racecheck_unittest demo_tests bigtest

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_C=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.c
DYNAMIC_ANNOTATIONS_O=dynamic_annotations.o
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_C} ${DYNAMIC_ANNOTATIONS_H}
GOOGLETEST_ROOT=../third_party/googletest

racecheck_unittest: racecheck_unittest.o old_test_suite.o posix_tests.o demo_tests.o \
		${GOOGLETEST_ROOT}/make/gtest_main.a ${DYNAMIC_ANNOTATIONS_O}
	${CXX} -lpthread $^ -o $@

demo_tests: old_test_suite.o demo_tests.o \
		${GOOGLETEST_ROOT}/make/gtest_main.a ${DYNAMIC_ANNOTATIONS_O}
	${CXX} -lpthread $^ -o $@

bigtest: bigtest.o ${DYNAMIC_ANNOTATIONS_O}
	${CXX} -lpthread $^ -o $@

${GOOGLETEST_ROOT}/make/gtest_main.a:
	cd ${GOOGLETEST_ROOT}/make && make clean && make

%.o: %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h
	${CXX} ${CXXFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -I${GOOGLETEST_ROOT}/include \
		 -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c -o $@

${DYNAMIC_ANNOTATIONS_O}: ${DYNAMIC_ANNOTATIONS_C}
	${CC} ${CFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function \
		-std=c89 -I${DYNAMIC_ANNOTATIONS_DIR} -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
		-c -o $@

# TODO(timurrrr): do smarter dependency list
win: racecheck_unittest.cc old_test_suite.cc windows_tests.cc demo_tests.cc ${DYNAMIC_ANNOTATIONS} \
     thread_wrappers.h thread_wrappers_win.h old_test_suite.h test_utils.h
	cd ${GOOGLETEST_ROOT}/msvc && devenv /build debug /project gtest gtest.sln
	cl.exe ${CXXFLAGS} /Od /Zi /EHsc /nologo /MTd \
		/D "DYNAMIC_ANNOTATIONS_ENABLED=1" /D "WIN32" \
		-I${DYNAMIC_ANNOTATIONS_DIR} -I${GOOGLETEST_ROOT}/include \
		racecheck_unittest.cc old_test_suite.cc windows_tests.cc demo_tests.cc \
		${DYNAMIC_ANNOTATIONS_C} ${GOOGLETEST_ROOT}/msvc/gtest/Debug/gtestd.lib


clean:
	rm -rf racecheck_unittest demo_tests bigtest *.o *.O1 *.O2 *.obj *.dSYM *.exe *.pdb *.ilk *.idb *.manifest
	cd ${GOOGLETEST_ROOT}/make && make clean
