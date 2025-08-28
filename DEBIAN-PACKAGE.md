# QEMU ZeroTier Debian Package

This document explains how to build and install the QEMU ZeroTier network backend as a Debian package.

## Quick Start

```bash
# Build the package
./build-deb.sh

# Install the package
sudo dpkg -i ../qemu-system-zerotier_*.deb
sudo apt-get install -f  # Fix dependencies if needed
```

## Package Contents

**qemu-system-zerotier**: Main QEMU binaries with ZeroTier support
- `qemu-system-x86_64` - x86_64 system emulator
- `qemu-system-aarch64` - ARM64 system emulator
- ZeroTier network backend support
- All standard QEMU system emulation features

**qemu-zerotier-utils**: QEMU utilities
- `qemu-img` - Disk image utility
- `qemu-io` - Disk I/O testing tool
- `qemu-nbd` - Network Block Device server

## Usage

Once installed, you can use the ZeroTier network backend:

```bash
# Start VM with ZeroTier network
qemu-system-x86_64 \
  -m 1G \
  -netdev user,id=net0 \
  -device virtio-net,netdev=net0 \
  -netdev zerotier,id=zt0,network=YOUR_NETWORK_ID,storage=/var/lib/qemu-zerotier \
  -device virtio-net,netdev=zt0 \
  -cdrom your-iso.iso
```

## ZeroTier Network Backend Features

- **High Performance**: 800+ Mbps throughput, 1-7ms latency
- **Layer 2 Integration**: Direct Ethernet-level ZeroTier access
- **Persistent Identity**: Maintains ZeroTier credentials across reboots
- **Automatic MAC Assignment**: Uses ZeroTier-assigned MAC addresses
- **Dual Network Support**: Can combine with NAT/user networking

## Requirements

- **ZeroTier Network**: Must have "Allow Ethernet Bridging" enabled
- **Network Authorization**: Private networks require authorization in ZeroTier Central
- **Storage Directory**: Writable directory for ZeroTier state persistence

## Build Dependencies

The build script automatically installs these on Debian/Ubuntu:

```
debhelper-compat, meson, ninja-build, pkg-config, python3,
libglib2.0-dev, libgnutls28-dev, libpixman-1-dev, libslirp-dev,
libgtk-3-dev, libspice-server-dev, libssh-dev, and more...
```

## Manual Build Steps

If the automated script doesn't work:

```bash
# 1. Install build dependencies
sudo apt-get build-dep qemu-system

# 2. Build ZeroTier static library
cd third_party
git clone https://github.com/zerotier/ZeroTierOne.git zerotier-src
cd zerotier-src && make -j$(nproc) one
mkdir -p ../zerotier
ar rcs ../zerotier/libzerotier.a node/*.o controller/*.o service/*.o ext/miniupnpc/*.o osdep/*.o

# 3. Build Debian package  
cd ../../
dpkg-buildpackage -b -us -uc
```

## Testing

After installation, test with a simple Alpine Linux VM:

```bash
# Download Alpine ISO
wget https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/x86_64/alpine-extended-3.22.1-x86_64.iso

# Start VM with ZeroTier
qemu-system-x86_64 \
  -m 1G -smp 2 \
  -netdev user,id=net0 \
  -device virtio-net,netdev=net0 \
  -netdev zerotier,id=zt0,network=bb720a5aae484765,storage=/tmp/zt-test \
  -device virtio-net,netdev=zt0 \
  -cdrom alpine-extended-3.22.1-x86_64.iso \
  -boot d
```

Inside the VM:
```bash
# Configure networking
udhcpc -i eth0  # NAT interface for internet
# eth1 should get ZeroTier IP automatically

# Test connectivity
ip addr show
ping YOUR_ZEROTIER_IP
```

## Troubleshooting

**Package build fails**: Ensure all build dependencies are installed
**ZeroTier not connecting**: Check network ID and ensure "Allow Ethernet Bridging" is enabled
**Permission denied**: Ensure storage directory is writable by QEMU process
**No network**: May need to run `arping` from VM for bidirectional connectivity

## Architecture Support

Currently supports:
- **x86_64 (amd64)**: Full support
- **aarch64 (arm64)**: Full support  

Other architectures can be added by modifying `debian/rules` target list.