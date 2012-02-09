
all: lint lib test

lib:
	$(MAKE) -C tsan

test:
	$(MAKE) -C tests

run: all
	(ulimit -s 8192; tests/tsan_test)

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint:
	third_party/cpplint/cpplint.py \
		--filter=$(RTL_LINT_FITLER) \
		tsan/*.cc tsan/*.h tests/*.cc tests/*.h

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
	cd tsan && $(MAKE) clean
	cd tests && $(MAKE) clean

