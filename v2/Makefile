CXX=g++
CXXFLAGS=-Wall -Werror -fPIE

ifeq ($(DEBUG), )
	CXXFLAGS+=-O2 -DNDEBUG
else
	CXXFLAGS+=-g -D_DEBUG
endif

LIBTSAN_HEADERS=llvm/tsan_interceptors.h \
                llvm/tsan_interface.h \
                tsan/tsan_linux.h \
                tsan/tsan_rtl.h


LIBTSAN_OBJ=tsan_rtl.o \
	    tsan_linux.o \
            tsan_interceptors.o 

%.o: tsan/%.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

%.o: llvm/%.cc Makefile $(LIBTSAN_HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

all: libtsan.a lint

libtsan.a: $(LIBTSAN_OBJ)
	ar ru $@ $(LIBTSAN_OBJ)

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint:
	third_party/cpplint/cpplint.py \
		--filter=$(RTL_LINT_FITLER) \
		tsan/*.cc tsan/*.h llvm/*.cc llvm/*.h test/*.cc test/*.h

get_third_party:
	rm -rf third_party
	mkdir third_party
	(cd third_party && \
	svn co -r375        http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r69 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )

test: libtsan.a
	$(MAKE) -C test
	test/tsan_test

clean:
	rm -f *.o *.a
	$(MAKE) clean -C test
