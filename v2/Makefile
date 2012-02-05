CXX=g++
CXXFLAGS=-O2 -g -Wall -Werror -fPIE

LIBTSAN_HEADERS=tsan_interceptors.h \
                tsan_interface.h \
                tsan_linux.h \
                tsan_rtl.h


LIBTSAN_OBJ=tsan_rtl.o \
	    tsan_linux.o \
            tsan_interceptors.o 

%.o: %.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

all: libtsan.a lint

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint:
	third_party/cpplint/cpplint.py \
		--filter=$(RTL_LINT_FITLER) \
		*.cc *.h test/*.cc test/*.h

get_third_party:
	rm -rf third_party
	mkdir third_party
	(cd third_party && \
	svn co -r375        http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r69 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )

clean:
	rm -f *.o *.a
	$(MAKE) clean -C test
