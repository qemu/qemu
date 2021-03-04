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
import os
from time import sleep
from tempfile import TemporaryDirectory

def get_args():
    parser = argparse.ArgumentParser(description="A gdbstub test runner")
    parser.add_argument("--qemu", help="Qemu binary for test",
                        required=True)
    parser.add_argument("--qargs", help="Qemu arguments for test")
    parser.add_argument("--binary", help="Binary to debug",
                        required=True)
    parser.add_argument("--test", help="GDB test script",
                        required=True)
    parser.add_argument("--gdb", help="The gdb binary to use",
                        default=None)
    parser.add_argument("--output", help="A file to redirect output to")

    return parser.parse_args()


def log(output, msg):
    if output:
        output.write(msg + "\n")
        output.flush()
    else:
        print(msg)


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
    if args.output:
        output = open(args.output, "w")
    else:
        output = None

    socket_dir = TemporaryDirectory("qemu-gdbstub")
    socket_name = os.path.join(socket_dir.name, "gdbstub.socket")

    # Launch QEMU with binary
    if "system" in args.qemu:
        cmd = "%s %s %s -gdb unix:path=%s,server=on" % (args.qemu,
                                                        args.qargs,
                                                        args.binary,
                                                        socket_name)
    else:
        cmd = "%s %s -g %s %s" % (args.qemu, args.qargs, socket_name,
                                  args.binary)

    log(output, "QEMU CMD: %s" % (cmd))
    inferior = subprocess.Popen(shlex.split(cmd))

    # Now launch gdb with our test and collect the result
    gdb_cmd = "%s %s" % (args.gdb, args.binary)
    # run quietly and ignore .gdbinit
    gdb_cmd += " -q -n -batch"
    # disable prompts in case of crash
    gdb_cmd += " -ex 'set confirm off'"
    # connect to remote
    gdb_cmd += " -ex 'target remote %s'" % (socket_name)
    # finally the test script itself
    gdb_cmd += " -x %s" % (args.test)


    sleep(1)
    log(output, "GDB CMD: %s" % (gdb_cmd))

    result = subprocess.call(gdb_cmd, shell=True, stdout=output)

    # A negative result is the result of an internal gdb failure like
    # a crash. We force a return of 0 so we don't fail the test on
    # account of broken external tools.
    if result < 0:
        print("GDB crashed? SKIPPING")
        exit(0)

    try:
        inferior.wait(2)
    except subprocess.TimeoutExpired:
        print("GDB never connected? Killed guest")
        inferior.kill()

    exit(result)
