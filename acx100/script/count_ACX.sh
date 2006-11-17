#!/bin/sh

# Useful to identify which files contain PCI/USB
# specific parts, and how many of them

{
echo "    ACX_PCI"
grep -c '^#if ACX_PCI$' * | sort -t: -k2,99 -r | grep -v :0$
echo "    ACX_USB"
grep -c '^#if ACX_USB$' * | sort -t: -k2,99 -r | grep -v :0$
} | $PAGER
