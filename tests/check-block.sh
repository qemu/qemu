#!/bin/sh

export QEMU_PROG="$(pwd)/x86_64-softmmu/qemu-system-x86_64"
export QEMU_IMG_PROG="$(pwd)/qemu-img"
export QEMU_IO_PROG="$(pwd)/qemu-io"

if [ ! -x $QEMU_PROG ]; then
    echo "'make check-block' requires qemu-system-x86_64"
    exit 1
fi

cd $SRC_PATH/tests/qemu-iotests

ret=0
./check -T -nocache -raw || ret=1
./check -T -nocache -qcow2 || ret=1
./check -T -nocache -qed|| ret=1
./check -T -nocache -vmdk|| ret=1
./check -T -nocache -vpc || ret=1

exit $ret
