#!/bin/bash

g++ test_dyn.cc -o libdyn.so -Wall -g3 -fPIC -shared
g++ test_dyn2.cc -o libdyn2.so -Wall -g3 -fPIC -shared
g++ bfd_symbolizer.c test.cc -o test -Wall -g3 -lbfd -lpthread -liberty -ldl -L. -ldyn


