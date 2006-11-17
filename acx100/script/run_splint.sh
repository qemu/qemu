#!/bin/sh
# has to be run from acx100 root dir
splint +posix-lib +showscan +showsummary -D__KERNEL__ -DMODULE -D__i686__ -Iinclude -I/usr/src/linux-2.6.3_splint_include/include -I/usr/include src/*.c &>/tmp/splint.log
