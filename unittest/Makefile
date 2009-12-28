CXX=g++

SUFFIX=
CXXFLAGS=

all: racecheck_unittest bigtest

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_CC=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.cc
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_CC} ${DYNAMIC_ANNOTATIONS_H}

racecheck_unittest: racecheck_unittest.o old_test_suite.o ${DYNAMIC_ANNOTATIONS}
	g++ -lpthread racecheck_unittest.o old_test_suite.o ${DYNAMIC_ANNOTATIONS_CC} -o $@

bigtest: bigtest.o
	g++ -lpthread bigtest.o ${DYNAMIC_ANNOTATIONS_CC} -o $@

%.o: %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_pthread.h old_test_suite.h test_utils.h
	${CXX} ${CXXFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow -Wno-unused-function  \
		 -I${DYNAMIC_ANNOTATIONS_DIR} -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 -c -o $@

win: racecheck_unittest.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers.h thread_wrappers_win.h
	cl.exe ${CXXFLAGS} -DDYNAMIC_ANNOTATIONS_ENABLED=1 -DWIN32 \
		-I${DYNAMIC_ANNOTATIONS_DIR} racecheck_unittest.cc ${DYNAMIC_ANNOTATIONS_CC}

clean:
	rm -rf racecheck_unittest bigtest *.o *.O1 *.O2 *.obj *.dSYM
