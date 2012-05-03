
all: lib test

help:
	@ echo "A little help is always welcome!"
	@ echo "The most useful targets are:"
	@ echo "  make install_deps  # Install third-party dependencies required for building"
	@ echo "  make presubmit     # Run it every time before committing"
	@ echo "  make lint          # Run the style checker"
	@ echo
	@ echo "For more info, see http://code.google.com/p/data-race-test/wiki/ThreadSanitizer2"

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
	svn co -r613 http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r82 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )

# Remove verbose printf from lint. Not strictly necessary.
hack_cpplint:
	sed -i "s/  sys.stderr.write('Done processing.*//g" third_party/cpplint/cpplint.py

clean:
	rm -f asm_*.s libtsan.nm libtsan.objdump
	$(MAKE) clean -C tsan
	$(MAKE) clean -C rtl_tests
	$(MAKE) clean -C unit_tests
