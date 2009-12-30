CXX=g++
CC=gcc

SUFFIX=
CXXFLAGS=

all: racecheck_unittest bigtest

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_C=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.c
DYNAMIC_ANNOTATIONS_O=dynamic_annotations.o
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_C} ${DYNAMIC_ANNOTATIONS_H}

racecheck_unittest: racecheck_unittest.o old_test_suite.o posix_tests.o ${DYNAMIC_ANNOTATIONS_O}
	g++ -lpthread $^ -o $@

bigtest: bigtest.o ${DYNAMIC_ANNOTATIONS_O}
	g++ -lpthread $^ -o $@

%.o: %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h
	${CXX} ${CXXFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function  \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c -o $@

${DYNAMIC_ANNOTATIONS_O}: ${DYNAMIC_ANNOTATIONS_C}
	${CC} ${CFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function \
		-std=c89 -I${DYNAMIC_ANNOTATIONS_DIR} -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
		-c -o $@

win: racecheck_unittest.cc old_test_suite.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_win.h old_test_suite.h test_utils.h
	cl.exe ${CXXFLAGS} /Od /Zi /EHsc /nologo \
		/D "DYNAMIC_ANNOTATIONS_ENABLED=1" /D "WIN32" \
		-I${DYNAMIC_ANNOTATIONS_DIR} \
		racecheck_unittest.cc old_test_suite.cc \
		${DYNAMIC_ANNOTATIONS_C}

clean:
	rm -rf racecheck_unittest bigtest *.o *.O1 *.O2 *.obj *.dSYM *.exe *.pdb *.ilk *.idb *.manifest
