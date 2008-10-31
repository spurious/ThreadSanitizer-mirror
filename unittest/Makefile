CXX=g++


default: 64

64:
	${CXX}  racecheck_unittest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o bin
		
	${CXX}  bigtest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o bigtest	

clean:
	rm bin bigtest
