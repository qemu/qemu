#!/bin/bash

SWD=$(cd $(dirname $0); pwd)

echo $SWD
function myecho 
{ 
    echo -e "\033[32m$@\033[0m";
}

function build_windows32()
{
    myecho "Now start build qemu-ga on windows 32bit"
    $SWD/configure --disable-werror --enable-guest-agent --cross-prefix=i686-w64-mingw32- --with-vss-sdk=$SWD/VSSSDK72

    if test $? -eq 0; then
        make qemu-ga.exe -j8
        #make qemu-ga-x86_64.msi
    fi
}

function build_windows64()
{
    myecho "Now start build qemu-ga on windows 64bit"

    $SWD/configure --disable-werror --enable-guest-agent --cross-prefix=x86_64-w64-mingw32- --with-vss-sdk=$SWD/VSSSDK72

    if test $? -eq 0; then
        make qemu-ga.exe -j8
        #make qemu-ga-x86_64.msi
    fi
}

function build_linux32()
{
    myecho "Now start build qemu-ga on linux 32bit"
}

function build_linux64()
{
    myecho "Now start build qemu-ga on linux 64bit"

    $SWD/configure --prefix= --target-list=x86_64-softmmu --enable-guest-agent --extra-cflags="-m64"

    if test $? -eq 0; then
        make qemu-ga -j8
    fi
}

function install_centos7_64_build_env()
{
    yum groups install -y "Development Tools"
    yum install -y zlib-devel glib2-devel
}


myecho "\n1 linux   32"
myecho   "2 linux   64"
myecho   "3 windows 32"
myecho   "4 windows 64"
myecho   "5 install_centos7_64_build_env\n"

read -p "Please input your choice num: " num

case $num in
    1)
        build_linux32
        exit 0
        ;;
    2)
        build_linux64
        exit 0
        ;;
    3)
        build_windows32
        exit 0
        ;;
    4)
        build_windows64
        exit 0
        ;;
    5)
        install_centos7_64_build_env
        exit 0
        ;;
    *)
        exit 1
        ;;
esac

