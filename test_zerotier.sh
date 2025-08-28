#!/bin/bash

# ZeroTier QEMU Test Script
# This script tests the ZeroTier network backend with Alpine Linux

QEMU_X86_64="./build/qemu-system-x86_64-unsigned"
QEMU_AARCH64="./build/qemu-system-aarch64-unsigned"
ALPINE_ISO="/Users/syso/Downloads/alpine-extended-3.22.1-x86_64.iso"
ALPINE_DISK="./alpine.qcow2"
ZEROTIER_NETWORK="bb720a5aae484765"
# Use persistent storage in home directory
ZT_STORAGE_DIR="${HOME}/.qemu-zerotier-test"

echo "ZeroTier QEMU Network Backend Test"
echo "=================================="
echo "Network ID: $ZEROTIER_NETWORK"
echo "Storage: $ZT_STORAGE_DIR"
echo "Alpine ISO: $ALPINE_ISO"
echo ""

# Check if files exist
if [ ! -f "$QEMU_X86_64" ]; then
    echo "Error: QEMU x86_64 binary not found at $QEMU_X86_64"
    echo "Make sure QEMU is built successfully"
    exit 1
fi

if [ ! -f "$ALPINE_ISO" ]; then
    echo "Error: Alpine ISO not found at $ALPINE_ISO"
    echo "Please download Alpine Linux ISO to the specified location"
    exit 1
fi

# Create storage directory
mkdir -p "$ZT_STORAGE_DIR"

echo "Starting QEMU with ZeroTier network backend..."
echo "Network backend: zerotier,network=$ZEROTIER_NETWORK,storage=$ZT_STORAGE_DIR"
echo ""
echo "VM Configuration:"
echo "- Memory: 1GB"
echo "- CPU: 2 cores"
echo "- Disk: ${ALPINE_DISK} (8GB)"
echo "- eth0: NAT (internet access)"
echo "- eth1: ZeroTier network $ZEROTIER_NETWORK"
echo "- Boot: Alpine Linux Live ISO"
echo ""
echo "Once Alpine boots, you can:"
echo "1. Check network interfaces with 'ip addr' (eth0=NAT, eth1=ZeroTier)"
echo "2. Configure eth0 for internet: 'udhcpc -i eth0'"
echo "3. Configure eth1 for ZeroTier (should get IP automatically)"
echo "4. Install Alpine to disk: 'setup-alpine' (use /dev/vda as disk)"
echo "5. Test connectivity with ping"
echo ""
echo "Press Ctrl+Alt+G to release mouse capture"
echo "Press Ctrl+Alt+Q to quit QEMU"
echo ""

# Launch QEMU with ZeroTier network backend
# Use a specific MAC address that both ZeroTier and the VM will use
# ZeroTier will pick its own MAC, but we need to ensure VM uses the same one
# For simplicity, we'll use the MAC that ZeroTier assigns and pass it to e1000
"$QEMU_X86_64" \
    -machine accel=tcg \
    -cpu qemu64 \
    -smp 2 \
    -m 1G \
    -drive file="$ALPINE_DISK",format=qcow2,if=virtio \
    -cdrom "$ALPINE_ISO" \
    -boot order=cd \
    -netdev user,id=net0 \
    -device virtio-net,netdev=net0,mac=52:54:00:12:34:56 \
    -netdev zerotier,id=zt0,network="$ZEROTIER_NETWORK",storage="$ZT_STORAGE_DIR" \
    -device virtio-net,netdev=zt0 \
    -display cocoa \
    -serial stdio
