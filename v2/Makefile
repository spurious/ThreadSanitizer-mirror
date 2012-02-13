
all: lint lib test

lib:
	$(MAKE) -C tsan

test: lib
	$(MAKE) -C tests

run: all
	(ulimit -s 8192; tests/tsan_c_test && tests/tsan_test)

presubmit:
	$(MAKE) clean run DEBUG=0
	$(MAKE) clean run DEBUG=1

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard
LINT_CMD=third_party/cpplint/cpplint.py --filter=$(RTL_LINT_FITLER)
LINT_FILES_CC+=$(wildcard tsan/*.cc)
LINT_FILES_CC+=$(wildcard tests/*.cc)
LINT_FILES_H+=$(wildcard tsan/*.h)
LINT_FILES_H+=$(wildcard tests/*.h)
LINT_OUT+=$(patsubst %.cc,%.cc.linted,$(LINT_FILES_CC))
LINT_OUT+=$(patsubst %.h,%.h.linted,$(LINT_FILES_H))

lint: $(LINT_OUT)

%.linted: %
	$(LINT_CMD) $<
	echo done > $@

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
	rm -f tsan/*.linted tests/*.linted
