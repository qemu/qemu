#!/usr/bin/env bash

img1="ubuntu-22.04-image0.img" # Renamed for clarity, was 'img'
user_data1="user-data0.img"
REPLICA_SIZE="256M"

# --- QEMU Execution (Example for Image 1 - adapt as needed) ---
# You'll need to decide how you want to run QEMU for each image.
# This is the original QEMU command, now pointing to img1 and user_data1.
# You would duplicate and modify this section for img2.

./build/qemu-system-x86_64 \
    -m 16G \
    -smp 12 \
    -drive "file=${img1},format=qcow2" \
    -drive "file=${user_data1},format=raw" \
    -nographic \
    -enable-kvm \
    -vga std \
    -device virtio-net-pci,netdev=net0 -netdev user,id=net0 \
    -object memory-backend-file,id=my-memdev0,size=${REPLICA_SIZE},mem-path=/home/jotham/qemu-cxl-shm/replica0.img,share=on \
    -object memory-backend-file,id=my-memdev1,size=${REPLICA_SIZE},mem-path=/home/jotham/qemu-cxl-shm/replica1.img,share=on \
    -object memory-backend-file,id=my-memdev2,size=${REPLICA_SIZE},mem-path=/home/jotham/qemu-cxl-shm/replica2.img,share=on \
    -device cxl-switch,id=cxlsw0,mem-size=${REPLICA_SIZE},memdev0=my-memdev0,memdev1=my-memdev1,memdev2=my-memdev2 \
    -virtfs local,path=qemu_share,mount_tag=shared,security_model=mapped-xattr
