#!/bin/bash

# Copyright 2009 Google Inc. All Rights Reserved.
# Author: timurrrr@google.com (Timur Iskhodzhanov)

wget http://www.pintool.org/cgi-bin/download.cgi?file=pin-2.6-24110-gcc.4.0.0-ia32_intel64-linux.tar.gz
tar xvfz pin-2.6-24110-gcc.4.0.0-ia32_intel64-linux.tar.gz
mv pin-2.5-24110-gcc.4.0.0-ia32_intel64-linux pin # note the wrong name of the
                                                  # extracted directory :-)

wget http://valgrind.org/downloads/valgrind-3.4.1.tar.bz2
tar xvjf valgrind-3.4.1.tar.bz2
mv valgrind-3.4.1 valgrind
