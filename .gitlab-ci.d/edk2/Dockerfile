#
# Docker image to cross-compile EDK2 firmware binaries
#
FROM ubuntu:18.04

MAINTAINER Philippe Mathieu-Daudé <f4bug@amsat.org>

# Install packages required to build EDK2
RUN apt update \
    && \
    \
    DEBIAN_FRONTEND=noninteractive \
    apt install --assume-yes --no-install-recommends \
        build-essential \
        ca-certificates \
        dos2unix \
        gcc-aarch64-linux-gnu \
        gcc-arm-linux-gnueabi \
        git \
        iasl \
        make \
        nasm \
        python3 \
        uuid-dev \
    && \
    \
    rm -rf /var/lib/apt/lists/*
