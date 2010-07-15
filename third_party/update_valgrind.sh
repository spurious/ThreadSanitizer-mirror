#!/bin/bash

update_subversion() {
  svn up
}

checkout() {
  echo "No directory 'valgrind'; doing svn checkout"
  svn co -r 24 http://valgrind-variant.googlecode.com/svn/trunk/valgrind valgrind
}

if [[ -d valgrind ]]; then
  cd valgrind && update_subversion
else
  checkout && cd valgrind
fi

