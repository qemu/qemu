#!/bin/bash

dmesg -c > /dev/null
insmod kernelinfo24.o > /dev/null 2>&1 || true
dmesg | awk '/---KERNELINFO-BEGIN---/{flag=1;next}/---KERNELINFO-END---/{flag=0}flag' > kernelinfo.txt
