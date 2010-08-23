#!/bin/bash

REV=59

update_subversion() {
  svn up -r$REV
}

checkout() {
  echo "No directory 'valgrind'; doing svn checkout"
  svn co -r $REV http://valgrind-variant.googlecode.com/svn/trunk/valgrind valgrind
}

if [[ -d valgrind ]]; then
  cd valgrind && update_subversion
else
  checkout && cd valgrind
fi

