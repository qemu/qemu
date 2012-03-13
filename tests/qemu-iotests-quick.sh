#!/bin/sh

# We don't know which of the system emulator binaries there is (or if there is
# any at all), so the 'quick' group doesn't contain any tests that require
# running qemu proper. Assign a fake binary name so that qemu-iotests doesn't
# complain about the missing binary.
export QEMU_PROG="this_should_be_unused"

export QEMU_IMG_PROG="$(pwd)/qemu-img"
export QEMU_IO_PROG="$(pwd)/qemu-io"

cd $SRC_PATH/tests/qemu-iotests

ret=0
./check -T -nocache -qcow2 -g quick || ret=1

exit $ret
