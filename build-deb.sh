#!/bin/bash

# QEMU ZeroTier Debian Package Build Script
set -e

echo "Building QEMU ZeroTier Debian Package"
echo "===================================="

# Check if we're on the right branch
CURRENT_BRANCH=$(git branch --show-current)
if [ "$CURRENT_BRANCH" != "zerotier-network-backend" ]; then
    echo "Warning: Not on zerotier-network-backend branch (current: $CURRENT_BRANCH)"
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Install build dependencies (if on Debian/Ubuntu)
if command -v apt-get &> /dev/null; then
    echo "Installing build dependencies..."
    sudo apt-get update
    sudo apt-get install -y \
        debhelper-compat \
        dh-make \
        devscripts \
        build-essential \
        fakeroot \
        meson \
        ninja-build \
        pkg-config \
        python3 \
        python3-sphinx \
        python3-sphinx-rtd-theme \
        libaio-dev \
        libbz2-dev \
        libcap-dev \
        libcap-ng-dev \
        libcurl4-gnutls-dev \
        libfdt-dev \
        libgbm-dev \
        libglib2.0-dev \
        libgnutls28-dev \
        libibverbs-dev \
        libjpeg-dev \
        libnuma-dev \
        libpixman-1-dev \
        libpng-dev \
        libsasl2-dev \
        libsdl2-dev \
        libslirp-dev \
        libspice-protocol-dev \
        libspice-server-dev \
        libssh-dev \
        libudev-dev \
        libusb-1.0-0-dev \
        libusbredirparser-dev \
        libzstd-dev \
        zlib1g-dev \
        libgtk-3-dev \
        libvte-2.91-dev
fi

# Ensure ZeroTier static library exists
echo "Checking ZeroTier static library..."
if [ ! -f "third_party/zerotier/libzerotier.a" ]; then
    echo "Building ZeroTier static library..."
    mkdir -p third_party
    cd third_party
    
    if [ ! -d "zerotier-src" ]; then
        git clone https://github.com/zerotier/ZeroTierOne.git zerotier-src
    fi
    
    cd zerotier-src
    make clean || true
    make -j$(nproc) one
    
    mkdir -p ../zerotier/include ../zerotier/node ../zerotier/osdep
    
    # Copy headers
    cp -r include/* ../zerotier/include/
    cp node/ZeroTierOne.h ../zerotier/include/
    
    # Create static library
    ar rcs ../zerotier/libzerotier.a \
        node/*.o \
        controller/*.o \
        service/*.o \
        ext/miniupnpc/*.o \
        osdep/*.o
    
    cd ../..
    echo "ZeroTier static library built successfully"
fi

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf build/
debian/rules clean || true

# Build source package
echo "Building source package..."
dpkg-buildpackage -S -us -uc

# Build binary packages
echo "Building binary packages..."
dpkg-buildpackage -b -us -uc

echo ""
echo "Build completed successfully!"
echo "Packages created:"
ls -la ../qemu-*zerotier*.deb 2>/dev/null || echo "No .deb files found in parent directory"
ls -la ../qemu-*zerotier*.changes 2>/dev/null || echo "No .changes files found in parent directory"

echo ""
echo "To install the packages:"
echo "sudo dpkg -i ../qemu-system-zerotier_*.deb ../qemu-zerotier-utils_*.deb"
echo "sudo apt-get install -f  # Fix any dependency issues"

echo ""
echo "To test the ZeroTier backend:"
echo "qemu-system-x86_64 \\"
echo "  -netdev zerotier,id=zt0,network=YOUR_NETWORK_ID,storage=/tmp/zt-storage \\"
echo "  -device virtio-net,netdev=zt0 \\"
echo "  -m 1G -cdrom your-iso.iso"