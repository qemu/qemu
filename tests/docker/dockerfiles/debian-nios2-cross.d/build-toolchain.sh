#!/bin/bash

set -e

TARGET=nios2-linux-gnu
LINUX_ARCH=nios2

J=$(expr $(nproc) / 2)
TOOLCHAIN_INSTALL=/usr/local
TOOLCHAIN_BIN=${TOOLCHAIN_INSTALL}/bin
CROSS_SYSROOT=${TOOLCHAIN_INSTALL}/$TARGET/sys-root

export PATH=${TOOLCHAIN_BIN}:$PATH

#
# Grab all of the source for the toolchain bootstrap.
#

wget https://ftp.gnu.org/gnu/binutils/binutils-2.37.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-11.2.0/gcc-11.2.0.tar.xz
wget https://ftp.gnu.org/gnu/glibc/glibc-2.34.tar.xz
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.70.tar.xz

tar axf binutils-2.37.tar.xz
tar axf gcc-11.2.0.tar.xz
tar axf glibc-2.34.tar.xz
tar axf linux-5.10.70.tar.xz

mv binutils-2.37 src-binu
mv gcc-11.2.0 src-gcc
mv glibc-2.34 src-glibc
mv linux-5.10.70 src-linux

mkdir -p bld-hdr bld-binu bld-gcc bld-glibc
mkdir -p ${CROSS_SYSROOT}/usr/include

#
# Install kernel and glibc headers
#

cd src-linux
make headers_install ARCH=${LINUX_ARCH} INSTALL_HDR_PATH=${CROSS_SYSROOT}/usr
cd ..

cd bld-hdr
../src-glibc/configure --prefix=/usr --host=${TARGET}
make install-headers DESTDIR=${CROSS_SYSROOT}
touch ${CROSS_SYSROOT}/usr/include/gnu/stubs.h
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
# Build gcc, without shared libraries, because we do not yet
# have a shared libc against which to link.
#

cd bld-gcc
../src-gcc/configure --disable-werror --disable-shared \
  --prefix=${TOOLCHAIN_INSTALL} --with-sysroot --target=${TARGET} \
  --enable-languages=c --disable-libssp --disable-libsanitizer \
  --disable-libatomic --disable-libgomp --disable-libquadmath
make -j${J}
make install
cd ..

#
# Build glibc
# There are a few random things that use c++ but we didn't build that
# cross-compiler.  We can get away without them.  Disable CXX so that
# glibc doesn't try to use the host c++ compiler.
#

cd bld-glibc
CXX=false ../src-glibc/configure --prefix=/usr --host=${TARGET}
make -j${j}
make install DESTDIR=${CROSS_SYSROOT}
cd ..
