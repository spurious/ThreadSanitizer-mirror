#!/bin/bash
dot -Tpng $1 > $1.png
qiv $1.png
rm -f $1.png
