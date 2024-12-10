#!/bin/bash
if [ "$#" -lt 1 ]; then
   echo "run.sh [libpanda*.so]"
   exit
fi

LIBPANDA=$1

gdb $LIBPANDA -ex "source pandare_build.py" -ex "extract_types" -ex "q"