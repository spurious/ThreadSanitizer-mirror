# simple make file, nothing special. 

HELGRIND='valgrind --tool=helgrind' 
TEST=0
CXX=g++

all: build 

build: racecheck_unittest

racecheck_unittest: racecheck_unittest.cc thread_wrappers_pthread.h dynamic_annotations.cc dynamic_annotations.h
	$(CXX) -DDYNAMIC_ANNOTATIONS=1  racecheck_unittest.cc dynamic_annotations.cc -lpthread -g -o $@

run: 
	$(HELGRIND) --suppressions=helgrind.supp  ./racecheck_unittest $(TEST)

clean: 
	rm -f racecheck_unittest 
