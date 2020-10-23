#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This takes a crashing qtest trace and tries to remove superflous operations
"""

import sys
import os
import subprocess
import time
import struct

QEMU_ARGS = None
QEMU_PATH = None
TIMEOUT = 5
CRASH_TOKEN = None

write_suffix_lookup = {"b": (1, "B"),
                       "w": (2, "H"),
                       "l": (4, "L"),
                       "q": (8, "Q")}

def usage():
    sys.exit("""\
Usage: QEMU_PATH="/path/to/qemu" QEMU_ARGS="args" {} input_trace output_trace
By default, will try to use the second-to-last line in the output to identify
whether the crash occred. Optionally, manually set a string that idenitifes the
crash by setting CRASH_TOKEN=
""".format((sys.argv[0])))

def check_if_trace_crashes(trace, path):
    global CRASH_TOKEN
    with open(path, "w") as tracefile:
        tracefile.write("".join(trace))

    rc = subprocess.Popen("timeout -s 9 {timeout}s {qemu_path} {qemu_args} 2>&1\
    < {trace_path}".format(timeout=TIMEOUT,
                           qemu_path=QEMU_PATH,
                           qemu_args=QEMU_ARGS,
                           trace_path=path),
                          shell=True,
                          stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE)
    stdo = rc.communicate()[0]
    output = stdo.decode('unicode_escape')
    if rc.returncode == 137:    # Timed Out
        return False
    if len(output.splitlines()) < 2:
        return False

    if CRASH_TOKEN is None:
        CRASH_TOKEN = output.splitlines()[-2]

    return CRASH_TOKEN in output


def minimize_trace(inpath, outpath):
    global TIMEOUT
    with open(inpath) as f:
        trace = f.readlines()
    start = time.time()
    if not check_if_trace_crashes(trace, outpath):
        sys.exit("The input qtest trace didn't cause a crash...")
    end = time.time()
    print("Crashed in {} seconds".format(end-start))
    TIMEOUT = (end-start)*5
    print("Setting the timeout for {} seconds".format(TIMEOUT))
    print("Identifying Crashes by this string: {}".format(CRASH_TOKEN))

    i = 0
    newtrace = trace[:]
    # For each line
    while i < len(newtrace):
        # 1.) Try to remove it completely and reproduce the crash. If it works,
        # we're done.
        prior = newtrace[i]
        print("Trying to remove {}".format(newtrace[i]))
        # Try to remove the line completely
        newtrace[i] = ""
        if check_if_trace_crashes(newtrace, outpath):
            i += 1
            continue
        newtrace[i] = prior

        # 2.) Try to replace write{bwlq} commands with a write addr, len
        # command. Since this can require swapping endianness, try both LE and
        # BE options. We do this, so we can "trim" the writes in (3)
        if (newtrace[i].startswith("write") and not
            newtrace[i].startswith("write ")):
            suffix = newtrace[i].split()[0][-1]
            assert(suffix in write_suffix_lookup)
            addr = int(newtrace[i].split()[1], 16)
            value = int(newtrace[i].split()[2], 16)
            for endianness in ['<', '>']:
                data = struct.pack("{end}{size}".format(end=endianness,
                                   size=write_suffix_lookup[suffix][1]),
                                   value)
                newtrace[i] = "write {addr} {size} 0x{data}\n".format(
                    addr=hex(addr),
                    size=hex(write_suffix_lookup[suffix][0]),
                    data=data.hex())
                if(check_if_trace_crashes(newtrace, outpath)):
                    break
            else:
                newtrace[i] = prior

        # 3.) If it is a qtest write command: write addr len data, try to split
        # it into two separate write commands. If splitting the write down the
        # middle does not work, try to move the pivot "left" and retry, until
        # there is no space left. The idea is to prune unneccessary bytes from
        # long writes, while accommodating arbitrary MemoryRegion access sizes
        # and alignments.
        if newtrace[i].startswith("write "):
            addr = int(newtrace[i].split()[1], 16)
            length = int(newtrace[i].split()[2], 16)
            data = newtrace[i].split()[3][2:]
            if length > 1:
                leftlength = int(length/2)
                rightlength = length - leftlength
                newtrace.insert(i+1, "")
                while leftlength > 0:
                    newtrace[i] = "write {addr} {size} 0x{data}\n".format(
                            addr=hex(addr),
                            size=hex(leftlength),
                            data=data[:leftlength*2])
                    newtrace[i+1] = "write {addr} {size} 0x{data}\n".format(
                            addr=hex(addr+leftlength),
                            size=hex(rightlength),
                            data=data[leftlength*2:])
                    if check_if_trace_crashes(newtrace, outpath):
                        break
                    else:
                        leftlength -= 1
                        rightlength += 1
                if check_if_trace_crashes(newtrace, outpath):
                    i -= 1
                else:
                    newtrace[i] = prior
                    del newtrace[i+1]
        i += 1
    check_if_trace_crashes(newtrace, outpath)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()

    QEMU_PATH = os.getenv("QEMU_PATH")
    QEMU_ARGS = os.getenv("QEMU_ARGS")
    if QEMU_PATH is None or QEMU_ARGS is None:
        usage()
    # if "accel" not in QEMU_ARGS:
    #     QEMU_ARGS += " -accel qtest"
    CRASH_TOKEN = os.getenv("CRASH_TOKEN")
    QEMU_ARGS += " -qtest stdio -monitor none -serial none "
    minimize_trace(sys.argv[1], sys.argv[2])
