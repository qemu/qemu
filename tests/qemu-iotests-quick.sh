#!/bin/sh

cd tests/qemu-iotests

ret=0
./check -T -qcow2 -g quick || ret=1

exit $ret
