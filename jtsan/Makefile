# Pass the correct path to MTRAT.
MTRAT=mtrat
TESTS=

# Build the unittests.
ThreadSanitizerTest.class: ThreadSanitizerTest.java
	javac ThreadSanitizerTest.java

# Run the unittests natively.
run: ThreadSanitizerTest.class
	java -ea -cp . ThreadSanitizerTest ${TESTS}

# Run the unittests under MTRAT.
mtrat: ThreadSanitizerTest.class
	$(MTRAT) -ea -cp . ThreadSanitizerTest ${TESTS}

# Clean
clean:
	rm -f *.class
