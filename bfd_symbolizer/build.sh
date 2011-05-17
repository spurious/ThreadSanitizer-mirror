#!/bin/bash

g++ bfd_symbolizer.c test.cc -o test -Wall -g -lbfd -lpthread -liberty


