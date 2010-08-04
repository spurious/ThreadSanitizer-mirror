#!/bin/bash
JTSAN_ROOT=${JTSAN_ROOT:-`dirname $0`/../third_party/java-thread-sanitizer/dist}
JTSAN_LOGFILE=${JTSAN_LOGFILE:-jtsan.events}
java \
  -Xbootclasspath/p:$JTSAN_ROOT/agent.jar \
  -javaagent:$JTSAN_ROOT/agent.jar=logfile=$JTSAN_LOGFILE:writer=str \
  "$@"
