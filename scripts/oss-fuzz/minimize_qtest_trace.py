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

# Minimization levels
M1 = False # try removing IO commands iteratively
M2 = False # try setting bits in operand of write/out to zero

write_suffix_lookup = {"b": (1, "B"),
                       "w": (2, "H"),
                       "l": (4, "L"),
                       "q": (8, "Q")}

def usage():
    sys.exit("""\
Usage:

QEMU_PATH="/path/to/qemu" QEMU_ARGS="args" {} [Options] input_trace output_trace

By default, will try to use the second-to-last line in the output to identify
whether the crash occred. Optionally, manually set a string that idenitifes the
crash by setting CRASH_TOKEN=

Options:

-M1: enable a loop around the remove minimizer, which may help decrease some
     timing dependant instructions. Off by default.
-M2: try setting bits in operand of write/out to zero. Off by default.

""".format((sys.argv[0])))

deduplication_note = """\n\
Note: While trimming the input, sometimes the mutated trace triggers a different
type crash but indicates the same bug. Under this situation, our minimizer is
incapable of recognizing and stopped from removing it. In the future, we may
use a more sophisticated crash case deduplication method.
\n"""

def check_if_trace_crashes(trace, path):
    with open(path, "w") as tracefile:
        tracefile.write("".join(trace))

    rc = subprocess.Popen("timeout -s 9 {timeout}s {qemu_path} {qemu_args} 2>&1\
    < {trace_path}".format(timeout=TIMEOUT,
                           qemu_path=QEMU_PATH,
                           qemu_args=QEMU_ARGS,
                           trace_path=path),
                          shell=True,
                          stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE,
                          encoding="utf-8")
    global CRASH_TOKEN
    if CRASH_TOKEN is None:
        try:
            outs, _ = rc.communicate(timeout=5)
            CRASH_TOKEN = " ".join(outs.splitlines()[-2].split()[0:3])
        except subprocess.TimeoutExpired:
            print("subprocess.TimeoutExpired")
            return False
        print("Identifying Crashes by this string: {}".format(CRASH_TOKEN))
        global deduplication_note
        print(deduplication_note)
        return True

    for line in iter(rc.stdout.readline, ""):
        if "CLOSED" in line:
            return False
        if CRASH_TOKEN in line:
            return True

    print("\nWarning:")
    print("  There is no 'CLOSED'or CRASH_TOKEN in the stdout of subprocess.")
    print("  Usually this indicates a different type of crash.\n")
    return False


# If previous write commands write the same length of data at the same
# interval, we view it as a hint.
def split_write_hint(newtrace, i):
    HINT_LEN = 3 # > 2
    if i <=(HINT_LEN-1):
        return None

    #find previous continuous write traces
    k = 0
    l = i-1
    writes = []
    while (k != HINT_LEN and l >= 0):
        if newtrace[l].startswith("write "):
            writes.append(newtrace[l])
            k += 1
            l -= 1
        elif newtrace[l] == "":
            l -= 1
        else:
            return None
    if k != HINT_LEN:
        return None

    length = int(writes[0].split()[2], 16)
    for j in range(1, HINT_LEN):
        if length != int(writes[j].split()[2], 16):
            return None

    step = int(writes[0].split()[1], 16) - int(writes[1].split()[1], 16)
    for j in range(1, HINT_LEN-1):
        if step != int(writes[j].split()[1], 16) - \
            int(writes[j+1].split()[1], 16):
            return None

    return (int(writes[0].split()[1], 16)+step, length)


def remove_lines(newtrace, outpath):
    remove_step = 1
    i = 0
    while i < len(newtrace):
        # 1.) Try to remove lines completely and reproduce the crash.
        # If it works, we're done.
        if (i+remove_step) >= len(newtrace):
            remove_step = 1
        prior = newtrace[i:i+remove_step]
        for j in range(i, i+remove_step):
            newtrace[j] = ""
        print("Removing {lines} ...\n".format(lines=prior))
        if check_if_trace_crashes(newtrace, outpath):
            i += remove_step
            # Double the number of lines to remove for next round
            remove_step *= 2
            continue
        # Failed to remove multiple IOs, fast recovery
        if remove_step > 1:
            for j in range(i, i+remove_step):
                newtrace[j] = prior[j-i]
            remove_step = 1
            continue
        newtrace[i] = prior[0] # remove_step = 1

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
                newtrace[i] = prior[0]

        # 3.) If it is a qtest write command: write addr len data, try to split
        # it into two separate write commands. If splitting the data operand
        # from length/2^n bytes to the left does not work, try to move the pivot
        # to the right side, then add one to n, until length/2^n == 0. The idea
        # is to prune unneccessary bytes from long writes, while accommodating
        # arbitrary MemoryRegion access sizes and alignments.

        # This algorithm will fail under some rare situations.
        # e.g., xxxxxxxxxuxxxxxx (u is the unnecessary byte)

        if newtrace[i].startswith("write "):
            addr = int(newtrace[i].split()[1], 16)
            length = int(newtrace[i].split()[2], 16)
            data = newtrace[i].split()[3][2:]
            if length > 1:

                # Can we get a hint from previous writes?
                hint = split_write_hint(newtrace, i)
                if hint is not None:
                    hint_addr = hint[0]
                    hint_len = hint[1]
                    if hint_addr >= addr and hint_addr+hint_len <= addr+length:
                        newtrace[i] = "write {addr} {size} 0x{data}\n".format(
                            addr=hex(hint_addr),
                            size=hex(hint_len),
                            data=data[(hint_addr-addr)*2:\
                                (hint_addr-addr)*2+hint_len*2])
                        if check_if_trace_crashes(newtrace, outpath):
                            # next round
                            i += 1
                            continue
                        newtrace[i] = prior[0]

                # Try splitting it using a binary approach
                leftlength = int(length/2)
                rightlength = length - leftlength
                newtrace.insert(i+1, "")
                power = 1
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
                    # move the pivot to right side
                    if leftlength < rightlength:
                        rightlength, leftlength = leftlength, rightlength
                        continue
                    power += 1
                    leftlength = int(length/pow(2, power))
                    rightlength = length - leftlength
                if check_if_trace_crashes(newtrace, outpath):
                    i -= 1
                else:
                    newtrace[i] = prior[0]
                    del newtrace[i+1]
        i += 1


def clear_bits(newtrace, outpath):
    # try setting bits in operands of out/write to zero
    i = 0
    while i < len(newtrace):
        if (not newtrace[i].startswith("write ") and not
           newtrace[i].startswith("out")):
           i += 1
           continue
        # write ADDR SIZE DATA
        # outx ADDR VALUE
        print("\nzero setting bits: {}".format(newtrace[i]))

        prefix = " ".join(newtrace[i].split()[:-1])
        data = newtrace[i].split()[-1]
        data_bin = bin(int(data, 16))
        data_bin_list = list(data_bin)

        for j in range(2, len(data_bin_list)):
            prior = newtrace[i]
            if (data_bin_list[j] == '1'):
                data_bin_list[j] = '0'
                data_try = hex(int("".join(data_bin_list), 2))
                # It seems qtest only accepts padded hex-values.
                if len(data_try) % 2 == 1:
                    data_try = data_try[:2] + "0" + data_try[2:]

                newtrace[i] = "{prefix} {data_try}\n".format(
                        prefix=prefix,
                        data_try=data_try)

                if not check_if_trace_crashes(newtrace, outpath):
                    data_bin_list[j] = '1'
                    newtrace[i] = prior
        i += 1


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

    newtrace = trace[:]
    global M1, M2

    # remove lines
    old_len = len(newtrace) + 1
    while(old_len > len(newtrace)):
        old_len = len(newtrace)
        print("trace lenth = ", old_len)
        remove_lines(newtrace, outpath)
        if not M1 and not M2:
            break
        newtrace = list(filter(lambda s: s != "", newtrace))
    assert(check_if_trace_crashes(newtrace, outpath))

    # set bits to zero
    if M2:
        clear_bits(newtrace, outpath)
    assert(check_if_trace_crashes(newtrace, outpath))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()
    if "-M1" in sys.argv:
        M1 = True
    if "-M2" in sys.argv:
        M2 = True
    QEMU_PATH = os.getenv("QEMU_PATH")
    QEMU_ARGS = os.getenv("QEMU_ARGS")
    if QEMU_PATH is None or QEMU_ARGS is None:
        usage()
    # if "accel" not in QEMU_ARGS:
    #     QEMU_ARGS += " -accel qtest"
    CRASH_TOKEN = os.getenv("CRASH_TOKEN")
    QEMU_ARGS += " -qtest stdio -monitor none -serial none "
    minimize_trace(sys.argv[-2], sys.argv[-1])
