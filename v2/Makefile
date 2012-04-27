
all: lib test

lib:
	$(MAKE) -C tsan

test: lib
	$(MAKE) -C rtl_tests

run: all
	(ulimit -s 8192; rtl_tests/tsan_test)

presubmit:
	$(MAKE) lint -j 4
	# Debug build with clang.
	$(MAKE) clean
	$(MAKE) run DEBUG=1 -j 16
	./output_tests/test_output.sh
	# Release build gcc
	$(MAKE) clean
	$(MAKE) run DEBUG=0 -j 16
	./check_analyze.sh
	./output_tests/test_output.sh
	@ echo PRESUBMIT PASSED

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint: lint_tsan lint_tests
lint_tsan:
	third_party/cpplint/cpplint.py --filter=$(RTL_LINT_FITLER) tsan/*.{cc,h}
lint_tests:
	third_party/cpplint/cpplint.py --filter=$(RTL_LINT_FITLER) rtl_tests/*.{cc,h}

install_deps:
	svn co  http://llvm.org/svn/llvm-project/compiler-rt/trunk/lib/asan/interception
	rm -rf third_party
	mkdir third_party
	(cd third_party && \
	svn co -r375        http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r69 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )

clean:
	rm -f asm_*.s libtsan.nm libtsan.objdump
	$(MAKE) clean -C tsan
	$(MAKE) clean -C rtl_tests
