

POSITIVE_TESTS=go_race_1 go_race_2
NEGATIVE_TESTS=go_norace_1
TESTS=$(POSITIVE_TESTS) $(NEGATIVE_TESTS)
all: $(TESTS)

%.o: %.go
	gccgo -g -c -o $@ $<
%: %.o
	gccgo -g -static-libgo -lpthread -o $@ $<

clean:
	rm -vf *.o $(TESTS)
