

TESTS=go_race_1
all: $(TESTS)

%.o: %.go
	gccgo -g -c -o $@ $<
%: %.o
	gccgo -g -static-libgo -lpthread -o $@ $<

clean:
	rm -vf *.o $(TESTS)
