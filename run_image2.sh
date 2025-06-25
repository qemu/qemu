#!/usr/bin/env bash

img2="ubuntu-22.04-image1.img"
user_data2="user-data1.img"
REPLICA_SIZE="1024M"
SERVER_SOCKET_PATH="/tmp/cxl_switch_server.sock"


./build/qemu-system-x86_64 \
    -m 16G \
    -smp 12 \
    -drive "file=${img2},format=qcow2" \
    -drive "file=${user_data2},format=raw" \
    -nographic \
    -enable-kvm \
    -vga std \
    -device virtio-net-pci,netdev=net0 -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device cxl-switch-client,id=cxlsw0,socket-path="${SERVER_SOCKET_PATH}" \
    -virtfs local,path=qemu_share,mount_tag=shared,security_model=mapped-xattr
