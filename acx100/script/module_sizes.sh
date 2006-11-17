#!/bin/sh
# Useful in determining function sizes

# Adapt to where you kernel is
cd /usr/src2/kernel/linux-2.6.11-smp8k

{
size drivers/net/wireless/acx/acx.o
{
nm -B -S drivers/net/wireless/acx/acx.o
} | grep -E '^[[:xdigit:]]* [[:xdigit:]]* [Tt] ' | sort -k2,99 -r
} | $PAGER
