CXX=g++
CXXFLAGS=-g -O2
INCLUDES=-I..

LIBTSAN=../libtsan.a

tsan_test: tsan_test.cc $(LIBTSAN)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(LIBTSAN) -o $@

clean:
	rm -f tsan_test
