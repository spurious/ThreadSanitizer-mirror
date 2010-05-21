#!/bin/bash
# Run all tests in ThreadSanitizerTest and report results.

JTSAN_ROOT="../third_party/java-thread-sanitizer/dist"
TSAN_OFFLINE="../tsan/bin/x86-linux-debug-ts_offline"
TMPDIR="/tmp/$(basename $0).$$"
JAVALOG="$TMPDIR/java.log"
JTSAN_EVENTS="$TMPDIR/jtsan.events"
TSAN_OUT="$TMPDIR/tsan.out"

set -x
set -e
mkdir $TMPDIR
java -ea \
  -Xbootclasspath/p:$JTSAN_ROOT/agent.jar \
  -javaagent:$JTSAN_ROOT/agent.jar=logfile=${JTSAN_EVENTS} \
  ThreadSanitizerTest >"$JAVALOG" 2>&1
${TSAN_OFFLINE} < ${JTSAN_EVENTS} > ${TSAN_OUT}

./summarize.py "$TMPDIR"

rm -rf "$TMPDIR"
set +x
