# Select valgrind and VEX revisions which we used so far.
VALGRIND_REV=10886
VEX_REV=1920
TSAN_REV=1208

# This will create directory 'valgrind'.
svn co -r $VALGRIND_REV svn://svn.valgrind.org/valgrind/trunk valgrind
cd valgrind
svn up -r $VEX_REV      VEX/

# Get ThreadSanitizer. This will create directory 'tsan' and patch valgrind
svn co -r $TSAN_REV     http://data-race-test.googlecode.com/svn/trunk/tsan tsan
mkdir tsan/tests
touch tsan/tests/Makefile.am # Valgrind needs this file.
patch -p 0 < tsan/valgrind.patch
# Build the whole thing.
./autogen.sh && ./configure --prefix=`pwd`/inst && make -j 8 && make install

# Get and compile the unittests
cd ../
svn checkout -r $TSAN_REV http://data-race-test.googlecode.com/svn/trunk/unittest tsan_test
svn checkout -r $TSAN_REV http://data-race-test.googlecode.com/svn/trunk/dynamic_annotations
cd tsan_test/
make
# Check if the ThreadSanitizer works: 
cd ../
./valgrind/inst/bin/valgrind --tool=tsan --color tsan_test/racecheck_unittest 301
# You should now see the ThreadSanitizer's output.
# Done!
