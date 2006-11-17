#!/bin/sh

# Useful to identify debug leftovers

{
grep -r '^#if ' . \
| grep -v ':#if !*ACX_' \
| grep -v ':#if USB' \
| grep -v ':#if WIRELESS_EXT' \
| grep -v ':#if LINUX_VERSION_CODE' \

} | $PAGER
