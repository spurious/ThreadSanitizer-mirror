CXX=g++


all: racecheck_unittest bigtest

racecheck_unittest: racecheck_unittest.cc dynamic_annotations.h thread_wrappers_pthread.h
	${CXX}  racecheck_unittest.cc dynamic_annotations.cc -lpthread -g -DDYNAMIC_ANNOTATIONS=1 -o racecheck_unittest
bigtest: bigtest.cc dynamic_annotations.h thread_wrappers_pthread.h
	${CXX}  bigtest.cc dynamic_annotations.cc -lpthread -g -DDYNAMIC_ANNOTATIONS=1 -o bigtest

clean:
	rm -f racecheck_unittest bigtest
