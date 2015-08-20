#!/bin/bash

if [ $USER != "root" ]; then
    echo "This command must be run by root in order to mount tmpfs."
    exit 1
fi

QEMU_DIR=.
QEMU_IMG=$QEMU_DIR/qemu-img
QEMU_TEST=$QEMU_DIR/qemu-test

if [ ! -e $QEMU_IMG ]; then
    echo "$QEMU_IMG does not exist."
    exit 1;
fi

if [ ! -e $QEMU_TEST ]; then
    echo "$QEMU_TEST does not exist."
    exit 1;
fi

DATA_DIR=/var/ramdisk
TRUTH_IMG=$DATA_DIR/truth.raw
TEST_IMG=$DATA_DIR/test.qcow2
TEST_BASE=$DATA_DIR/zero-500M.raw
CMD_LOG=/tmp/test-qcow2.log

mount | grep $DATA_DIR > /dev/null
if [ $? -ne 0 ]; then
    echo "Create tmpfs at $DATA_DIR to store testing images."
    if [ ! -e $DATA_DIR ]; then mkdir -p $DATA_DIR ; fi
    mount -t tmpfs none $DATA_DIR -o size=4G
    if [ $? -ne 0 ]; then exit 1; fi
fi

parallel=100
round=100000
fail_prob=0
cancel_prob=0
instant_qemubh=true
seed=$RANDOM$RANDOM
count=0

function invoke() {
    echo "$*" >> $CMD_LOG
    $*
    if [ $? -ne 0 ]; then
        echo "Exit with error code $?: $*"
    fi
}

/bin/rm -f $CMD_LOG
touch $CMD_LOG

while [ -t ]; do
for cluster_size in 65536 7680 512 1024 15872 65024 1048576 1048064; do
for io_size in 10485760 ; do
    count=$[$count + 1]
    echo "Round $count" >> $CMD_LOG

    # QCOW2 image is about 1G
    img_size=$[(1073741824 + ($RANDOM$RANDOM$RANDOM % 104857600)) / 512 * 512]

    # base image is about 500MB
    base_size=$[(536870912 + ($RANDOM$RANDOM$RANDOM % 104857600)) / 512 * 512]

    invoke "/bin/rm -rf $TRUTH_IMG $TEST_IMG $TEST_BASE"
    invoke "dd if=/dev/zero of=$TRUTH_IMG count=0 bs=1 seek=$img_size"
    invoke "dd if=/dev/zero of=$TEST_BASE count=0 bs=1 seek=$base_size"
    invoke "$QEMU_IMG create -f qcow2 -ocluster_size=$cluster_size -b $TEST_BASE $TEST_IMG $img_size"

    invoke "$QEMU_TEST --seed=$seed --truth=$TRUTH_IMG --format=qcow2 --test="blksim:$TEST_IMG" --verify_write=true --compare_before=false --compare_after=true --round=$round --parallel=$parallel --io_size=$io_size --fail_prob=$fail_prob --cancel_prob=$cancel_prob --instant_qemubh=$instant_qemubh"

    seed=$[$seed + 1]
done; done; done
