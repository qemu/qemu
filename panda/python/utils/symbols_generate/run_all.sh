#!/bin/bash

ROOT=$(realpath ../../../../)
BUILD=$(realpath $ROOT/build)
PANDARE_AUTOGEN_DIR=$(realpath $ROOT/panda/python/core/pandare2/autogen)

LIBPANDAS=$(find $BUILD -maxdepth 1 -name "libpanda-*.so")

for LIBPANDA in $LIBPANDAS; do
    echo "Running $LIBPANDA"
    bash ./gdb_run.sh $LIBPANDA
done

mv _pandare_ffi_*.py $PANDARE_AUTOGEN_DIR