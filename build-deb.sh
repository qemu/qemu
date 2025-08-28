#!/bin/bash

# QEMU ZeroTier Debian Package Build Script
set -e

# Parse command line arguments
INSTALL_DEPS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --install-deps)
            INSTALL_DEPS=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--install-deps]"
            echo ""
            echo "Options:"
            echo "  --install-deps  Install build dependencies via apt-get"
            echo "  -h, --help      Show this help message"
            echo ""
            echo "Note: Dependencies are not installed by default."
            echo "Run with --install-deps if you need to install them."
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

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

# Install build dependencies (only if requested)
if [ "$INSTALL_DEPS" = true ] && command -v apt-get &> /dev/null; then
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
elif [ "$INSTALL_DEPS" = false ]; then
    echo "Skipping dependency installation (use --install-deps to install)"
fi

# Ensure ZeroTier static library exists
echo "Checking ZeroTier static library..."
if [ ! -f "third_party/zerotier/libzerotier.a" ]; then
    echo "Building ZeroTier static library..."
    mkdir -p third_party
    cd third_party
    
    # Clean up any failed previous attempts
    rm -rf zerotier-src zerotier
    
    echo "Cloning ZeroTier source..."
    git clone https://github.com/zerotier/ZeroTierOne.git zerotier-src
    cd zerotier-src
    
    echo "Building ZeroTier..."
    make clean 2>/dev/null || true
    make -j$(nproc) one
    
    # Check for either 'one' or 'zerotier-one' binary
    if [ -f "zerotier-one" ]; then
        echo "ZeroTier binary 'zerotier-one' built successfully"
    elif [ -f "one" ]; then
        echo "ZeroTier binary 'one' built successfully"
    else
        echo "Error: ZeroTier binary not built successfully"
        echo "Expected 'one' or 'zerotier-one' but found:"
        ls -la | grep -E "(one|zerotier)" || echo "No matching binaries found"
        exit 1
    fi
    
    # Create target directories
    mkdir -p ../zerotier/{include,node,osdep}
    
    # Copy headers - check for different possible locations
    if [ -d "include" ]; then
        echo "Copying headers from include/..."
        cp -r include/* ../zerotier/include/ 2>/dev/null || true
    fi
    
    # Copy main header file - try different locations
    if [ -f "node/ZeroTierOne.h" ]; then
        echo "Found ZeroTierOne.h in node/"
        cp node/ZeroTierOne.h ../zerotier/include/
    elif [ -f "include/ZeroTierOne.h" ]; then
        echo "Found ZeroTierOne.h in include/"
        cp include/ZeroTierOne.h ../zerotier/include/
    else
        echo "Error: Cannot find ZeroTierOne.h header file"
        echo "Available files in node/:"
        ls -la node/ | head -10
        echo "Available files in include/:"
        ls -la include/ 2>/dev/null | head -10 || echo "No include directory"
        exit 1
    fi
    
    # Create static library from object files
    echo "Creating static library..."
    OBJECT_FILES=""
    
    # Collect object files from different directories
    for dir in node controller service osdep; do
        if [ -d "$dir" ]; then
            OBJ_FILES=$(find "$dir" -name "*.o" 2>/dev/null || true)
            if [ -n "$OBJ_FILES" ]; then
                OBJECT_FILES="$OBJECT_FILES $OBJ_FILES"
                echo "Found $(echo $OBJ_FILES | wc -w) object files in $dir/"
            fi
        fi
    done
    
    # Add external library objects
    for extdir in ext/miniupnpc ext/libnatpmp ext/http-parser; do
        if [ -d "$extdir" ]; then
            EXT_OBJ_FILES=$(find "$extdir" -name "*.o" 2>/dev/null || true)
            if [ -n "$EXT_OBJ_FILES" ]; then
                OBJECT_FILES="$OBJECT_FILES $EXT_OBJ_FILES"
                echo "Found $(echo $EXT_OBJ_FILES | wc -w) object files in $extdir/"
            fi
        fi
    done
    
    # Add any rustybits static library if it exists
    if [ -f "rustybits/target/release/libzeroidc.a" ]; then
        echo "Found rustybits library, extracting objects..."
        mkdir -p ../zerotier/rust-objs
        cd ../zerotier/rust-objs
        ar x ../../zerotier-src/rustybits/target/release/libzeroidc.a
        RUST_OBJS=$(find . -name "*.o" 2>/dev/null || true)
        if [ -n "$RUST_OBJS" ]; then
            OBJECT_FILES="$OBJECT_FILES $RUST_OBJS"
        fi
        cd ../../zerotier-src
    fi
    
    if [ -z "$OBJECT_FILES" ]; then
        echo "Error: No object files found for static library creation"
        echo "Build may have failed. Checking for ZeroTier binaries..."
        ls -la | grep -E "(one|zerotier)" || echo "No ZeroTier binaries found"
        exit 1
    fi
    
    echo "Creating static library with $(echo $OBJECT_FILES | wc -w) object files..."
    ar rcs ../zerotier/libzerotier.a $OBJECT_FILES
    
    if [ ! -f "../zerotier/libzerotier.a" ]; then
        echo "Error: Failed to create static library"
        exit 1
    fi
    
    LIBSIZE=$(stat -f%z "../zerotier/libzerotier.a" 2>/dev/null || stat -c%s "../zerotier/libzerotier.a" 2>/dev/null || echo "unknown")
    echo "ZeroTier static library created successfully (size: ${LIBSIZE} bytes)"
    
    cd ../..
else
    echo "ZeroTier static library already exists"
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