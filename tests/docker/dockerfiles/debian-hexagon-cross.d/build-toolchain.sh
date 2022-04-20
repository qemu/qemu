#!/bin/bash

set -e

BASE=$(readlink -f ${PWD})

TOOLCHAIN_INSTALL=$(readlink -f "$TOOLCHAIN_INSTALL")
ROOTFS=$(readlink -f "$ROOTFS")

TOOLCHAIN_BIN=${TOOLCHAIN_INSTALL}/bin
HEX_SYSROOT=${TOOLCHAIN_INSTALL}/hexagon-unknown-linux-musl
HEX_TOOLS_TARGET_BASE=${HEX_SYSROOT}/usr

function cdp() {
  DIR="$1"
  mkdir -p "$DIR"
  cd "$DIR"
}

function fetch() {
  DIR="$1"
  URL="$2"
  TEMP="$(readlink -f "$PWD/tmp.tar.gz")"
  wget --quiet "$URL" -O "$TEMP"
  cdp "$DIR"
  tar xaf "$TEMP" --strip-components=1
  rm "$TEMP"
  cd -
}

build_llvm_clang() {
  fetch "$BASE/llvm-project" "$LLVM_URL"
  cdp "$BASE/build-llvm"

  cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${TOOLCHAIN_INSTALL} \
    -DLLVM_ENABLE_LLD=ON \
    -DLLVM_TARGETS_TO_BUILD="Hexagon" \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    "$BASE/llvm-project/llvm"
  ninja all install
  cd ${TOOLCHAIN_BIN}
  ln -sf clang hexagon-unknown-linux-musl-clang
  ln -sf clang++ hexagon-unknown-linux-musl-clang++
  ln -sf llvm-ar hexagon-unknown-linux-musl-ar
  ln -sf llvm-objdump hexagon-unknown-linux-musl-objdump
  ln -sf llvm-objcopy hexagon-unknown-linux-musl-objcopy
  ln -sf llvm-readelf hexagon-unknown-linux-musl-readelf
  ln -sf llvm-ranlib hexagon-unknown-linux-musl-ranlib

  # workaround for now:
  cat <<EOF > hexagon-unknown-linux-musl.cfg
-G0 --sysroot=${HEX_SYSROOT}
EOF
}

build_clang_rt() {
  cdp "$BASE/build-clang_rt"
  cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_CONFIG_PATH="$BASE/build-llvm/bin/llvm-config" \
    -DCMAKE_ASM_FLAGS="-G0 -mlong-calls -fno-pic --target=hexagon-unknown-linux-musl " \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_C_COMPILER="${TOOLCHAIN_BIN}/hexagon-unknown-linux-musl-clang" \
    -DCMAKE_ASM_COMPILER="${TOOLCHAIN_BIN}/hexagon-unknown-linux-musl-clang" \
    -DCMAKE_INSTALL_PREFIX=${HEX_TOOLS_TARGET_BASE} \
    -DCMAKE_CROSSCOMPILING=ON \
    -DCMAKE_C_COMPILER_FORCED=ON \
    -DCMAKE_CXX_COMPILER_FORCED=ON \
    -DCOMPILER_RT_BUILD_BUILTINS=ON \
    -DCOMPILER_RT_BUILTINS_ENABLE_PIC=OFF \
    -DCMAKE_SIZEOF_VOID_P=4 \
    -DCOMPILER_RT_OS_DIR= \
    -DCAN_TARGET_hexagon=1 \
    -DCAN_TARGET_x86_64=0 \
    -DCOMPILER_RT_SUPPORTED_ARCH=hexagon \
    -DLLVM_ENABLE_PROJECTS="compiler-rt" \
    "$BASE/llvm-project/compiler-rt"
  ninja install-compiler-rt
}

build_musl_headers() {
  fetch "$BASE/musl" "$MUSL_URL"
  cd "$BASE/musl"
  make clean
  CC=${TOOLCHAIN_BIN}/hexagon-unknown-linux-musl-clang \
    CROSS_COMPILE=hexagon-unknown-linux-musl \
    LIBCC=${HEX_TOOLS_TARGET_BASE}/lib/libclang_rt.builtins-hexagon.a \
    CROSS_CFLAGS="-G0 -O0 -mv65 -fno-builtin -fno-rounding-math --target=hexagon-unknown-linux-musl" \
    ./configure --target=hexagon --prefix=${HEX_TOOLS_TARGET_BASE}
  PATH=${TOOLCHAIN_BIN}:$PATH make CROSS_COMPILE= install-headers

  cd ${HEX_SYSROOT}/..
  ln -sf hexagon-unknown-linux-musl hexagon
}

build_kernel_headers() {
  fetch "$BASE/linux" "$LINUX_URL"
  mkdir -p "$BASE/build-linux"
  cd "$BASE/linux"
  make O=../build-linux ARCH=hexagon \
   KBUILD_CFLAGS_KERNEL="-mlong-calls" \
   CC=${TOOLCHAIN_BIN}/hexagon-unknown-linux-musl-clang \
   LD=${TOOLCHAIN_BIN}/ld.lld \
   KBUILD_VERBOSE=1 comet_defconfig
  make mrproper

  cd "$BASE/build-linux"
  make \
    ARCH=hexagon \
    CC=${TOOLCHAIN_BIN}/clang \
    INSTALL_HDR_PATH=${HEX_TOOLS_TARGET_BASE} \
    V=1 \
    headers_install
}

build_musl() {
  cd "$BASE/musl"
  make clean
  CROSS_COMPILE=hexagon-unknown-linux-musl- \
    AR=llvm-ar \
    RANLIB=llvm-ranlib \
    STRIP=llvm-strip \
    CC=clang \
    LIBCC=${HEX_TOOLS_TARGET_BASE}/lib/libclang_rt.builtins-hexagon.a \
    CFLAGS="-G0 -O0 -mv65 -fno-builtin -fno-rounding-math --target=hexagon-unknown-linux-musl" \
    ./configure --target=hexagon --prefix=${HEX_TOOLS_TARGET_BASE}
  PATH=${TOOLCHAIN_BIN}/:$PATH make CROSS_COMPILE= install
  cd ${HEX_TOOLS_TARGET_BASE}/lib
  ln -sf libc.so ld-musl-hexagon.so
  ln -sf ld-musl-hexagon.so ld-musl-hexagon.so.1
  cdp ${HEX_TOOLS_TARGET_BASE}/../lib
  ln -sf ../usr/lib/ld-musl-hexagon.so.1
}

build_llvm_clang
build_kernel_headers
build_musl_headers
build_clang_rt
build_musl
