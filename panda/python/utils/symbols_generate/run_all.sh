#!/bin/bash

ROOT=$(realpath ../../../../)
BUILD=$(realpath $ROOT/build)
PANDARE_AUTOGEN_DIR=$(realpath $ROOT/panda/python/core/autogen)

LIBPANDAS=$(find $BUILD -name "libpanda*.so")

for LIBPANDA in $LIBPANDAS; do
    echo "Running $LIBPANDA"
    bash ./gdb_run.sh $LIBPANDA
done

mv _pandare_ffi_* $PANDARE_AUTOGEN_DIR