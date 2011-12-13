#!/bin/bash
# This script will download and extract apache sources.
# See also: http://httpd.apache.org/download.cgi
set -e
set -x
wget http://www.sai.msu.su/apache/httpd/httpd-2.2.21.tar.bz2
tar xf httpd-2.2.21.tar.bz2

