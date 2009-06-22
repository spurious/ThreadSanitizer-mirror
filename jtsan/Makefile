

# Pass the correct path to junit.jar.
# On Ubuntu, use this to install JUnit: 
#   sudo apt-get install junit4
JUNIT=/usr/share/java/junit4.jar
# Pass the correct path to MTRAT.
MTRAT=mtrat

# Build the unittests.
ThreadSanitizerTest.class: ThreadSanitizerTest.java
	javac -cp $(JUNIT) ThreadSanitizerTest.java

# Run the unittests natively.
run: ThreadSanitizerTest.class
	java -cp .:$(JUNIT) ThreadSanitizerTest

# Run the unittests under MTRAT.
mtrat: ThreadSanitizerTest.class
	$(MTRAT) -cp .:$(JUNIT) ThreadSanitizerTest

# Clean
clean:
	rm -f *.class
