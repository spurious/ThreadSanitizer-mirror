#!/bin/bash
# Script to add ThreadSanitizer into existing valgrind directory

VALGRIND_REV=10454
VEX_REV=1908
TSAN_REV=1092
VALGRIND_DIR=`pwd`

check_revisions() {
  if ! svn info | grep "Revision: ${VALGRIND_REV}" >/dev/null; then
    return 1
  fi
  if ! svn info VEX | grep "Revision: ${VEX_REV}" >/dev/null; then
    return 1
  fi
    return 0
}

if ! check_revisions; then
  echo "ERROR:" >&2
  echo "Either you run the script from outside of the valgrind directory" >&2
  echo "or the Valgrind/VEX revision numbers are different those we used." >&2
  echo "Please sync with revisions (r${VALGRIND_REV}/r${VEX_REV}, respectively)." >&2
  exit 1
fi

set -x
set -e

cd ..
# Get ThreadSanitizer into directory 'tsan' and put it next to your valgrind directory.
svn checkout -r $TSAN_REV http://data-race-test.googlecode.com/svn/trunk/tsan tsan

cd $VALGRIND_DIR
# Create symlinks of tsan files (and some dummy files) in valgrind directory
mkdir tsan
cd tsan
ln -s ../../tsan/[A-Za-z]* .

# Create empty 'docs' and 'tests' directories - valgrind needs them.
mkdir docs tests
touch docs/Makefile.am tests/Makefile.am

# Apply a patch to the valgrind files.
cd ../
patch -p 0 < tsan/valgrind.patch
# Apply a patch to the VEX files.
cd VEX
patch -p 0 < ../tsan/vex.patch
