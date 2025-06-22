#!/usr/bin/env bash

REPLICA_DIR="/home/jotham/qemu-cxl-shm"
REPLICA_SIZE="1024"
REPLICA_1="$REPLICA_DIR/replica0.img"
REPLICA_2="$REPLICA_DIR/replica1.img"
REPLICA_3="$REPLICA_DIR/replica2.img"

make
./cxl_fm /tmp/cxl_switch_server.sock /tmp/cxl_switch_server_admin.sock $REPLICA_SIZE $REPLICA_1 $REPLICA_2 $REPLICA_3