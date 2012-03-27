
all: lib test

lib:
	$(MAKE) -C tsan

test: lib
	$(MAKE) -C tests

run: all
	(ulimit -s 8192; tests/tsan_c_test && tests/tsan_test)

presubmit:
	$(MAKE) lint -j 4
	# Debug build with clang.
	$(MAKE) clean
	$(MAKE) run DEBUG=1 -j 16
	./output_tests/test_output.sh
	# Release build gcc (ensure that vanilla gcc is able to build it).
	$(MAKE) clean
	$(MAKE) run DEBUG=0 -j 16 CC=/usr/bin/gcc CXX=/usr/bin/g++
	./output_tests/test_output.sh
	@ echo PRESUBMIT PASSED

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint: lint_tsan lint_tests
lint_tsan:
	third_party/cpplint/cpplint.py --filter=$(RTL_LINT_FITLER) tsan/*.{cc,h}
lint_tests:
	third_party/cpplint/cpplint.py --filter=$(RTL_LINT_FITLER) tests/*.{cc,h}

get_third_party:
	rm -rf third_party
	mkdir third_party
	(cd third_party && \
	svn co -r375        http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r69 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )
get_interception:
	svn co  http://llvm.org/svn/llvm-project/compiler-rt/trunk/lib/asan/interception
      
clean:
	rm -f *.s
	$(MAKE) clean -C tsan
	$(MAKE) clean -C tests
