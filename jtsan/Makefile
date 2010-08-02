# Pass the correct path to MTRAT.
MTRAT=mtrat
TESTS=-testPositive_WW_HashSetAccessNoLocks \
      -testPositive_WW_LockInBetween \
      -testNegative_CyclicBarrier \
      -testNegative_ReadWriteLock_TryLock \
      -testNegative_CountDownLatch

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
	&& ../tsan/bin/amd64-linux-debug-ts_offline --error_exitcode=1 <${JTSAN_LOGFILE} \
	|| (echo '========================================' && cat ${JTSAN_LOGFILE} && exit 1)
# Clean
clean:
	rm -fr *.class bin dist *log
