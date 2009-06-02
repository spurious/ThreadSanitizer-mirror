CXX=g++

SUFFIX=
CXXFLAGS=

all: racecheck_unittest${SUFFIX} bigtest${SUFFIX}
# + false_sharing_unittest${SUFFIX}
O1: 
	make SUFFIX=.O1 CXXFLAGS="-O1 -fno-inline"
O2: 
	make SUFFIX=.O2 CXXFLAGS="-O2 -fno-inline"
all_opt: all O1 O2

%${SUFFIX}: %.cc dynamic_annotations.h thread_wrappers_pthread.h
	${CXX} ${CXXFLAGS} $< -Wall dynamic_annotations.cc -lpthread -g -DDYNAMIC_ANNOTATIONS=1 -o $@

clean:
	rm -f racecheck_unittest bigtest *.O1 *.O2
