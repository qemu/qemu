#!/usr/bin/env python3
#
# Run a gdbstub test case
#
# Copyright (c) 2019 Linaro
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import subprocess
import shutil
import shlex

def get_args():
    parser = argparse.ArgumentParser(description="A gdbstub test runner")
    parser.add_argument("--qemu", help="Qemu binary for test",
                        required=True)
    parser.add_argument("--qargs", help="Qemu arguments for test")
    parser.add_argument("--binary", help="Binary to debug",
                        required=True)
    parser.add_argument("--test", help="GDB test script",
                        required=True)
    parser.add_argument("--gdb", help="The gdb binary to use", default=None)

    return parser.parse_args()

if __name__ == '__main__':
    args = get_args()

    # Search for a gdb we can use
    if not args.gdb:
        args.gdb = shutil.which("gdb-multiarch")
    if not args.gdb:
        args.gdb = shutil.which("gdb")
    if not args.gdb:
        print("We need gdb to run the test")
        exit(-1)

    # Launch QEMU with binary
    if "system" in args.qemu:
        cmd = "%s %s %s -s -S" % (args.qemu, args.qargs, args.binary)
    else:
        cmd = "%s %s -g 1234 %s" % (args.qemu, args.qargs, args.binary)

    inferior = subprocess.Popen(shlex.split(cmd))

    # Now launch gdb with our test and collect the result
    gdb_cmd = "%s %s -ex 'target remote localhost:1234' -x %s" % (args.gdb, args.binary, args.test)

    result = subprocess.call(gdb_cmd, shell=True);

    exit(result)
