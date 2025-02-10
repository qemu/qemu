#!/bin/bash

set -e

TARGET=microblaze-linux-musl
LINUX_ARCH=microblaze

J=$(expr $(nproc) / 2)
TOOLCHAIN_INSTALL=/usr/local
TOOLCHAIN_BIN=${TOOLCHAIN_INSTALL}/bin
CROSS_SYSROOT=${TOOLCHAIN_INSTALL}/$TARGET/sys-root

GCC_PATCH0_URL=https://raw.githubusercontent.com/Xilinx/meta-xilinx/refs/tags/xlnx-rel-v2024.1/meta-microblaze/recipes-devtools/gcc/gcc-12/0009-Patch-microblaze-Fix-atomic-boolean-return-value.patch

export PATH=${TOOLCHAIN_BIN}:$PATH

#
# Grab all of the source for the toolchain bootstrap.
#

wget https://ftp.gnu.org/gnu/binutils/binutils-2.37.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-11.2.0/gcc-11.2.0.tar.xz
wget https://www.musl-libc.org/releases/musl-1.2.2.tar.gz
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.70.tar.xz

tar axf binutils-2.37.tar.xz
tar axf gcc-11.2.0.tar.xz
tar axf musl-1.2.2.tar.gz
tar axf linux-5.10.70.tar.xz

mv binutils-2.37 src-binu
mv gcc-11.2.0 src-gcc
mv musl-1.2.2 src-musl
mv linux-5.10.70 src-linux

#
# Patch gcc
#

wget -O - ${GCC_PATCH0_URL} | patch -d src-gcc -p1

mkdir -p bld-hdr bld-binu bld-gcc bld-musl
mkdir -p ${CROSS_SYSROOT}/usr/include

#
# Install kernel headers
#

cd src-linux
make headers_install ARCH=${LINUX_ARCH} INSTALL_HDR_PATH=${CROSS_SYSROOT}/usr
cd ..

#
# Build binutils
#

cd bld-binu
../src-binu/configure --disable-werror \
  --prefix=${TOOLCHAIN_INSTALL} --with-sysroot --target=${TARGET}
make -j${J}
make install
cd ..

#
# Build gcc, just the compiler so far.
#

cd bld-gcc
../src-gcc/configure --disable-werror --disable-shared \
  --prefix=${TOOLCHAIN_INSTALL} --with-sysroot --target=${TARGET} \
  --enable-languages=c --disable-libssp --disable-libsanitizer \
  --disable-libatomic --disable-libgomp --disable-libquadmath
make -j${J} all-gcc
make install-gcc
cd ..

#
# Build musl.
# We won't go through the extra step of building shared libraries
# because we don't actually use them in QEMU docker testing.
#

cd bld-musl
../src-musl/configure --prefix=/usr --host=${TARGET} --disable-shared
make -j${j}
make install DESTDIR=${CROSS_SYSROOT}
cd ..

#
# Go back and build the compiler runtime
#

cd bld-gcc
make -j${j}
make install
cd ..
