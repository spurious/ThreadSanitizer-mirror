# Pass the correct path to MTRAT.
MTRAT=mtrat
TESTS=

# Build the unittests.
ThreadSanitizerTest.class: ThreadSanitizerTest.java RaceDetectorApi.java
	javac $^

# Run the unittests natively.
run: ThreadSanitizerTest.class
	java -ea -cp . ThreadSanitizerTest ${TESTS}

# Run the unittests under MTRAT.
mtrat: ThreadSanitizerTest.class
	$(MTRAT) -ea -cp . ThreadSanitizerTest ${TESTS}

# Run tests under ThreadSanitizer
tsan: ThreadSanitizerTest.class
	 java -Xbootclasspath/p:dist/agent.jar -javaagent:dist/agent.jar ThreadSanitizerTest ${TESTS}

# Clean
clean:
	rm -fr *.class bin dist *log
