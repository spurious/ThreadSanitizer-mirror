#!/bin/sh

for TEST in "$@"
do
  echo "=============================="
  SUPP=
  if [ -f output_tests/${TEST}.supp ]; then
    SUPP=--suppressions=output_tests/${TEST}.supp
  fi

  IGNORE=
  if [ -f output_tests/${TEST}.ignore ]; then
    IGNORE=--ignore=output_tests/${TEST}.ignore
  fi

  CMD="$TSAN $SUPP $IGNORE ./${BIN}output_tests/${TEST}-${BUILD}${EXE} 2>&1 | ./match_output.py output_tests/${TEST}.tmpl"
  echo "Running"
  echo "$ $CMD"
  $CMD
  echo "=============================="
  echo
done
