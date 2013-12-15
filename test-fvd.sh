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
TEST_IMG=$DATA_DIR/test.fvd
TEST_BASE=$DATA_DIR/zero-500M.raw
TEST_IMG_DATA=$DATA_DIR/test.dat
CMD_LOG=/tmp/test-fvd.log

mount | grep $DATA_DIR > /dev/null
if [ $? -ne 0 ]; then
    echo "Create tmpfs at $DATA_DIR to store testing images."
    if [ ! -e $DATA_DIR ]; then mkdir -p $DATA_DIR ; fi
    mount -t tmpfs none $DATA_DIR -o size=4G
    if [ $? -ne 0 ]; then exit 1; fi
fi

G1=1073741824
MAX_MEM=536870912
MAX_ROUND=1000000
MAX_IO_SIZE=100000000
fail_prob=0.1
cancel_prob=0.1
flush_prob=0.01
seed=$RANDOM$RANDOM
count=0

function invoke() {
    echo "$*" >> $CMD_LOG
    sync
    $*
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "$Exit with error code $ret: $*"
        exit $ret;
    fi
}

/bin/rm -f $CMD_LOG
touch $CMD_LOG

while [ -t ]; do
    for compact_image in on off ; do
    for prefetch_delay in 1 0; do
    for copy_on_read in on off; do
    for block_size in 7680 512 1024 15872 65536 65024 1048576 1048064; do
    for chunk_mult in 5 1 2 3 7 9 12 16 33 99 ; do
    for base_img in ""  "-b $TEST_BASE"; do
        chunk_size=$[$block_size * $chunk_mult]
        large_io_size=$[$chunk_size * 5]
        if [ $large_io_size -gt $MAX_IO_SIZE ]; then large_io_size=$MAX_IO_SIZE; fi
    for io_size in $large_io_size 1048576 ; do
    for use_data_file in "" "data_file=$TEST_IMG_DATA," ; do

        # FVD image is about 1G
        img_size=$[(1073741824 + ($RANDOM$RANDOM$RANDOM % 104857600)) / 512 * 512]

        # base image is about 500MB
        base_size=$[(536870912 + ($RANDOM$RANDOM$RANDOM % 104857600)) / 512 * 512]

        count=$[$count + 1]
        echo "Round $count" >> $CMD_LOG

        invoke "/bin/rm -rf $TRUTH_IMG $TEST_IMG $TEST_BASE $TEST_IMG_DATA"
        invoke "dd if=/dev/zero of=$TRUTH_IMG count=0 bs=1 seek=$img_size"
        invoke "dd if=/dev/zero of=$TEST_BASE count=0 bs=1 seek=$base_size"
        if [ ! -z $use_data_file ]; then invoke "touch $TEST_IMG_DATA"; fi

        mixed_records_per_journal_sector=121
        journal_size=$[(((($io_size / $chunk_size ) + 1 ) / $mixed_records_per_journal_sector ) + 1) * 512 * 100]

        invoke "$QEMU_IMG create -f fvd $base_img -o${use_data_file}data_file_fmt=blksim,compact_image=$compact_image,copy_on_read=$copy_on_read,block_size=$block_size,chunk_size=$chunk_size,journal_size=$journal_size,prefetch_start_delay=$prefetch_delay $TEST_IMG $img_size"
        if [ $prefetch_delay -eq 1 ]; then $QEMU_IMG update $TEST_IMG prefetch_over_threshold_throttle_time=0; fi

        # Use no more 1GB memory.
        mem=$[$io_size * 1000]
        if [ $mem -gt $MAX_MEM ]; then
            parallel=$[$MAX_MEM / $io_size]
        else
            parallel=1000
        fi
        parallel=$[${RANDOM}${RANDOM} % $parallel]

        round=$[$G1 * 10 / $io_size]
        if [ $round -gt $MAX_ROUND ]; then round=$MAX_ROUND; fi

        b3=$[$round * 2 / 3]
        [ $b3 -eq 0 ] && b3=1
        for rep in 0 1 2 ; do
            if [ $rep -eq 0 ]; then
                compare_before=false
            else
                compare_before=true
            fi
            r=$[${RANDOM}${RANDOM} % $b3]
            seed=$[$seed + 1]
            invoke "$QEMU_TEST --truth=$TRUTH_IMG --format=fvd --test="blksim:$TEST_IMG" --verify_write=true --parallel=$parallel --io_size=$io_size --fail_prob=$fail_prob --cancel_prob=$cancel_prob --flush_prob=$flush_prob --compare_after=true --round=$r --compare_before=$compare_before --seed=$seed"
        done

        /bin/rm -rf /tmp/fvd.log*
done; done; done; done; done; done; done; done; done
