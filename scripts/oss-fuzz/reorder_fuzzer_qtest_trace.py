#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Use this to convert qtest log info from a generic fuzzer input into a qtest
trace that you can feed into a standard qemu-system process. Example usage:

QEMU_FUZZ_ARGS="-machine q35,accel=qtest" QEMU_FUZZ_OBJECTS="*" \
        ./i386-softmmu/qemu-fuzz-i386 --fuzz-target=generic-pci-fuzz
# .. Finds some crash
QTEST_LOG=1 FUZZ_SERIALIZE_QTEST=1 \
QEMU_FUZZ_ARGS="-machine q35,accel=qtest" QEMU_FUZZ_OBJECTS="*" \
        ./i386-softmmu/qemu-fuzz-i386 --fuzz-target=generic-pci-fuzz
        /path/to/crash 2> qtest_log_output
scripts/oss-fuzz/reorder_fuzzer_qtest_trace.py qtest_log_output > qtest_trace
./i386-softmmu/qemu-fuzz-i386 -machine q35,accel=qtest \
        -qtest stdin < qtest_trace

### Details ###

Some fuzzer make use of hooks that allow us to populate some memory range, just
before a DMA read from that range. This means that the fuzzer can produce
activity that looks like:
    [start] read from mmio addr
    [end]   read from mmio addr
    [start] write to pio addr
        [start] fill a DMA buffer just in time
        [end]   fill a DMA buffer just in time
        [start] fill a DMA buffer just in time
        [end]   fill a DMA buffer just in time
    [end]   write to pio addr
    [start] read from mmio addr
    [end]   read from mmio addr

We annotate these "nested" DMA writes, so with QTEST_LOG=1 the QTest trace
might look something like:
[R +0.028431] readw 0x10000
[R +0.028434] outl 0xc000 0xbeef  # Triggers a DMA read from 0xbeef and 0xbf00
[DMA][R +0.034639] write 0xbeef 0x2 0xAAAA
[DMA][R +0.034639] write 0xbf00 0x2 0xBBBB
[R +0.028431] readw 0xfc000

This script would reorder the above trace so it becomes:
readw 0x10000
write 0xbeef 0x2 0xAAAA
write 0xbf00 0x2 0xBBBB
outl 0xc000 0xbeef
readw 0xfc000

I.e. by the time, 0xc000 tries to read from DMA, those DMA buffers have already
been set up, removing the need for the DMA hooks. We can simply provide this
reordered trace via -qtest stdio to reproduce the input

Note: this won't work for traces where the device tries to read from the same
DMA region twice in between MMIO/PIO commands. E.g:
    [R +0.028434] outl 0xc000 0xbeef
    [DMA][R +0.034639] write 0xbeef 0x2 0xAAAA
    [DMA][R +0.034639] write 0xbeef 0x2 0xBBBB

The fuzzer will annotate suspected double-fetches with [DOUBLE-FETCH]. This
script looks for these tags and warns the users that the resulting trace might
not reproduce the bug.
"""

import sys

__author__     = "Alexander Bulekov <alxndr@bu.edu>"
__copyright__  = "Copyright (C) 2020, Red Hat, Inc."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Alexander Bulekov"
__email__      = "alxndr@bu.edu"


def usage():
    sys.exit("Usage: {} /path/to/qtest_log_output".format((sys.argv[0])))


def main(filename):
    with open(filename, "r") as f:
        trace = f.readlines()

    # Leave only lines that look like logged qtest commands
    trace[:] = [x.strip() for x in trace if "[R +" in x
                or "[S +" in x and "CLOSED" not in x]

    for i in range(len(trace)):
        if i+1 < len(trace):
            if "[DMA]" in trace[i+1]:
                if "[DOUBLE-FETCH]" in trace[i+1]:
                    sys.stderr.write("Warning: Likely double fetch on line"
                                     "{}.\n There will likely be problems "
                                     "reproducing behavior with the "
                                     "resulting qtest trace\n\n".format(i+1))
                trace[i], trace[i+1] = trace[i+1], trace[i]
    for line in trace:
        print(line.split("]")[-1].strip())


if __name__ == '__main__':
    if len(sys.argv) == 1:
        usage()
    main(sys.argv[1])
