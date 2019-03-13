#!/bin/sh
# This work is licensed under the terms of the GNU LGPL, version 2 or later.
# See the COPYING.LIB file in the top-level directory.

PYTHON=$1
DECODETREE=$2
E=0

# All of these tests should produce errors
for i in err_*.decode; do
    if $PYTHON $DECODETREE $i > /dev/null 2> /dev/null; then
        # Pass, aka failed to fail.
        echo FAIL: $i 1>&2
        E=1
    fi
done

for i in succ_*.decode; do
    if ! $PYTHON $DECODETREE $i > /dev/null 2> /dev/null; then
        echo FAIL:$i 1>&2
    fi
done

exit $E
