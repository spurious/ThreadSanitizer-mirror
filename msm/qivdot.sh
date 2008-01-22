#!/bin/bash
png=`basename $1 .dor`.png
dot -Tpng $1 > $png
qiv $png
