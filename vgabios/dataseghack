#!/bin/bash

awk \
  'BEGIN { }\
  /^\.text/,/DATA_SEG_DEFS_HERE/ { print }\
  END { }'\
  $1 > temp.awk.1

awk \
  'BEGIN { i = 0; last = "hello" }\
  /BLOCK_STRINGS_BEGIN/,/^\.bss/ { if ( i > 1 ) { print last } last = $0; i = i + 1 }\
  END { }'\
  $1 > temp.awk.2

awk \
  'BEGIN { }\
  /DATA_SEG_DEFS_HERE/,/BLOCK_STRINGS_BEGIN/ { print }\
  END { }'\
  $1 > temp.awk.3

cp $1 $1.orig
cat temp.awk.1 temp.awk.2 temp.awk.3 | sed -e 's/^\.data//' -e 's/^\.bss//' -e 's/^\.text//' > $1
/bin/rm -f temp.awk.1 temp.awk.2 temp.awk.3 $1.orig
