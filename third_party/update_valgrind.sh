#!/bin/bash

VALGRIND_REV=10974
VEX_REV=1946

cd valgrind

svn up -r $VALGRIND_REV
svn up -r $VEX_REV VEX/
