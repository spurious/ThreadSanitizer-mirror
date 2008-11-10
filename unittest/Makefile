CXX=g++


all:
	${CXX}  racecheck_unittest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o racecheck_unittest
		
	${CXX}  bigtest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o bigtest	

clean:
	rm -f racecheck_unittest bigtest
