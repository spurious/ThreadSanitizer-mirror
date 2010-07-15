# Pass the correct path to MTRAT.
MTRAT=mtrat
TESTS=testNegative1 testNegative2

JTSAN_ROOT=../third_party/java-thread-sanitizer/dist
JTSAN_LOGFILE=jtsan.events

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
	./jtsan.sh ThreadSanitizerTest ${TESTS} \
	&& ../tsan/bin/amd64-linux-debug-ts_offline --error_exitcode=1 <${JTSAN_LOGFILE}

# Clean
clean:
	rm -fr *.class bin dist *log
