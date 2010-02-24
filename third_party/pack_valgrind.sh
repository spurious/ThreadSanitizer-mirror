#!/bin/bash

source valgrind_rev.sh

svn export -r $VALGRIND_REV --ignore-externals svn://svn.valgrind.org/valgrind/trunk valgrind
svn export -r $VEX_REV svn://svn.valgrind.org/vex/trunk valgrind/VEX
tar -cjvf valgrind-r${VALGRIND_REV}-vex-r${VEX_REV}.tar.bz2 valgrind
