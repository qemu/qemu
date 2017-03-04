#!/bin/bash
#
# Script for building QEMU on Windows in MSYS2 environment  
#

set -e

uname_output=`uname`

if [ $uname_output != MSYS_NT-6.1 ]; then
  echo "This script is only for building under MSYS2"
  exit 1
fi

package_list="python2 make pkg-config gcc zlib-devel glib2-devel autoconf bison automake1.10 libtool libutil-linux-devel"
if ! pacman -Q  $package_list > /dev/null; then
  echo "Missing dependencies. Please run:"
  echo "pacman -S $package_list"
  exit 1
fi

echo "Configuring..."
mkdir -p build
cd build
CC=/usr/bin/gcc ../configure --python=/usr/bin/python2 2>&1 | tee build.log

echo "Building..."
make -j6 2>&1 | tee -a build.log

echo "Done"
