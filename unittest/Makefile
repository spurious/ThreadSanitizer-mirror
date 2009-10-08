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

DYNAMIC_ANNOTATIONS_DIR=../dynamic_annotations
DYNAMIC_ANNOTATIONS_H=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.h
DYNAMIC_ANNOTATIONS_CC=${DYNAMIC_ANNOTATIONS_DIR}/dynamic_annotations.cc
DYNAMIC_ANNOTATIONS=${DYNAMIC_ANNOTATIONS_CC} ${DYNAMIC_ANNOTATIONS_H}

%${SUFFIX}: %.cc ${DYNAMIC_ANNOTATIONS} thread_wrappers_pthread.h
	${CXX} ${CXXFLAGS} $< -Wall -Werror -Wno-sign-compare -Wshadow  \
		 -I${DYNAMIC_ANNOTATIONS_DIR} ${DYNAMIC_ANNOTATIONS_CC} \
		 -lpthread -g -DDYNAMIC_ANNOTATIONS_ENABLED=1 -o $@

clean:
	rm -f racecheck_unittest bigtest *.O1 *.O2
