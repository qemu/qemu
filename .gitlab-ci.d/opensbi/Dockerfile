#
# Docker image to cross-compile OpenSBI firmware binaries
#
FROM ubuntu:18.04

MAINTAINER Bin Meng <bmeng.cn@gmail.com>

# Install packages required to build OpenSBI
RUN apt update \
    && \
    \
    DEBIAN_FRONTEND=noninteractive \
    apt install --assume-yes --no-install-recommends \
        build-essential \
        ca-certificates \
        git \
        make \
	python3 \
        wget \
    && \
    \
    rm -rf /var/lib/apt/lists/*

# Manually install the kernel.org "Crosstool" based toolchains for gcc-8.3
RUN wget -O - \
    https://mirrors.edge.kernel.org/pub/tools/crosstool/files/bin/x86_64/8.3.0/x86_64-gcc-8.3.0-nolibc-riscv32-linux.tar.xz \
    | tar -C /opt -xJ
RUN wget -O - \
    https://mirrors.edge.kernel.org/pub/tools/crosstool/files/bin/x86_64/8.3.0/x86_64-gcc-8.3.0-nolibc-riscv64-linux.tar.xz \
    | tar -C /opt -xJ

# Export the toolchains to the system path
ENV PATH="/opt/gcc-8.3.0-nolibc/riscv32-linux/bin:${PATH}"
ENV PATH="/opt/gcc-8.3.0-nolibc/riscv64-linux/bin:${PATH}"
