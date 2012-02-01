CXX=g++
CXXFLAGS=-O2 -g

LIBTSAN_OBJ=tsan_rtl.o \
	    tsan_linux.o

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $<

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

clean:
	rm -f *.o *.a
	$(MAKE) clean -C test
