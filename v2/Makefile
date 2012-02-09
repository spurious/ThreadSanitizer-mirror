
all: lint

RTL_LINT_FITLER=-legal/copyright,-build/include,-readability/casting,-build/header_guard

lint:
	third_party/cpplint/cpplint.py \
		--filter=$(RTL_LINT_FITLER) \
		tsan/*.cc tsan/*.h test/*.cc test/*.h

get_third_party:
	rm -rf third_party
	mkdir third_party
	(cd third_party && \
	svn co -r375        http://googletest.googlecode.com/svn/trunk googletest && \
	svn co -r69 http://google-styleguide.googlecode.com/svn/trunk/cpplint cpplint \
        )

test: libtsan.a
	$(MAKE) -C test

clean:
	rm -f *.o *.a
	$(MAKE) clean -C test
