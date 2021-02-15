#!/bin/sh
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# Summerise the state of code coverage with gcovr and tweak the output
# to be more sane on CI runner. As we expect to be executed on a
# throw away CI instance we do spam temp files all over the shop. You
# most likely don't want to execute this script but just call gcovr
# directly. See also "make coverage-report"
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

# first generate the coverage report
gcovr -p -o raw-report.txt

# strip the full-path and line markers
sed s@$PWD\/@@ raw-report.txt | sed s/[0-9]\*[,-]//g > simplified.txt

# reflow lines that got split
awk '/.[ch]$/ { printf("%s", $0); next } 1' simplified.txt > rejoined.txt

# columnify
column -t rejoined.txt > final.txt

# and dump, stripping out 0% coverage
grep -v "0%" final.txt
