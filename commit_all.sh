#!/bin/bash

(cd trunk/msm; make allpng)
(cd trunk; svn commit -m "$1")
(cd wiki; svn commit -m "wiki update")
