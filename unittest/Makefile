GCC = /home/timurrrr/mixed_gcc/g++


default: 64

64:
	${GCC}  racecheck_unittest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o bin
		
	${GCC}  bigtest.cc dynamic_annotations.cc -lpthread -g \
		-DDYNAMIC_ANNOTATIONS=1 -o bigtest	

clean:
	rm bin bigtest
