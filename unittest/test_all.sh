#!/bin/bash

make

export MSM_THREAD_SANITIZER=1
PARAM="$@"
TSAN=${TSAN:-tsan}

run() {
  env $ENV $TSAN $FLAGS \
    --error-exitcode=1 --gen-suppressions=all \
    --suppressions=racecheck_unittest.supp \
    ./racecheck_unittest $PARAM || exit 1
}

fast_mode() {
  ENV=TSAN_FAST_MODE=1
  FLAGS="--fast-mode=yes --pure-happens-before=no --ignore-in-dtor=yes"
  run
}

slow_mode() {
  ENV=
  FLAGS="--fast-mode=no  --pure-happens-before=no --ignore-in-dtor=no"
  run
}

pure_hb_mode() {
  ENV=TSAN_PURE_HAPPENS_BEFORE=1
  FLAGS="--pure-happens-before=yes --ignore-in-dtor=no"
  run
}

fast_mode
slow_mode
pure_hb_mode

