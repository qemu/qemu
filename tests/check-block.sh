#!/bin/sh

FORMAT_LIST="raw qcow2 qed vmdk vpc"
if [ "$#" -ne 0 ]; then
    FORMAT_LIST="$@"
fi

export QEMU_PROG="$PWD/x86_64-softmmu/qemu-system-x86_64"
export QEMU_IMG_PROG="$PWD/qemu-img"
export QEMU_IO_PROG="$PWD/qemu-io"

if [ ! -x $QEMU_PROG ]; then
    echo "'make check-block' requires qemu-system-x86_64"
    exit 1
fi

cd tests/qemu-iotests

ret=0
for FMT in $FORMAT_LIST ; do
    ./check -T -nocache -$FMT || ret=1
done

exit $ret
