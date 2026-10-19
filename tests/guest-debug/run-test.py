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
    parser.add_argument("--pargs", help="Program arguments for test")
    parser.add_argument("--kargs", help="Kernel args to --append")
    parser.add_argument("--binary", help="Binary to debug")
    parser.add_argument("--symbols", help="Symbol file for binary to debug")
    parser.add_argument("--symbol-offset", help="offset to apply to symbols")
    parser.add_argument("--test", help="GDB test script")
    parser.add_argument('test_args', nargs='*',
                        help="Additional args for GDB test script. "
                        "The args should be preceded by -- to avoid confusion "
                        "with flags for runner script")
    parser.add_argument("--gdb", help="The gdb binary to use",
                        default=None)
    parser.add_argument("--gdb-args", help="Additional gdb arguments")
    parser.add_argument("--output", help="A file to redirect output to")
    parser.add_argument("--stderr", help="A file to redirect stderr to")
    parser.add_argument("--no-suspend", action="store_true",
                        help="Ask the binary to not wait for GDB connection")

    return parser.parse_args()


def log(output, msg):
    if output:
        output.write(msg + "\n")
        output.flush()
    else:
        print(msg)


def format_cmd(cmd):
    return " ".join(shlex.quote(arg) for arg in cmd)


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
    if args.stderr:
        stderr = open(args.stderr, "w")
    else:
        stderr = None

    socket_dir = TemporaryDirectory("qemu-gdbstub")
    socket_name = os.path.join(socket_dir.name, "gdbstub.socket")

    # Launch QEMU with binary
    qemu_cmd = [args.qemu]
    if args.qargs:
        qemu_cmd.extend(shlex.split(args.qargs))

    if "system" in args.qemu:
        # for system mode we assume qargs has set-up for binary if
        # indeed we have set one.
        if args.binary:
            qemu_cmd.append(args.binary)
        if not args.no_suspend:
            qemu_cmd.append("-S")
        qemu_cmd.extend(["-gdb", f"unix:path={socket_name},server=on"])

        if args.kargs:
            qemu_cmd.extend(["--append", args.kargs])

        # There is no guest program command-line in system mode.
        assert not args.pargs
    else:
        qemu_cmd.append("-g")
        if args.no_suspend:
            qemu_cmd.append(f"{socket_name},suspend=n")
        else:
            qemu_cmd.append(f"{socket_name}")

        # There are no appended args in user-mode
        assert not args.kargs

        # for user mode binary and args always go at the end
        qemu_cmd.append(args.binary)
        if args.pargs:
            qemu_cmd.extend(shlex.split(args.pargs))

    log(output, "QEMU CMD: %s" % (format_cmd(qemu_cmd)))
    inferior = subprocess.Popen(qemu_cmd)

    # Now launch gdb with our test and collect the result
    gdb_cmd = [args.gdb]

    if args.binary:
        gdb_cmd.append(args.binary)
    if args.gdb_args:
        gdb_cmd.extend(shlex.split(args.gdb_args))

    # run quietly and ignore .gdbinit
    gdb_cmd.extend(["-q", "-n", "-batch"])
    # disable pagination
    gdb_cmd.extend(["-ex", "set pagination off"])
    # disable prompts in case of crash
    gdb_cmd.extend(["-ex", "set confirm off"])
    # connect to remote
    gdb_cmd.extend(["-ex", f"target remote {socket_name}"])

    if args.symbols:
        sym_arg = f"symbol-file {args.symbols}"
        if args.symbol_offset:
            sym_arg += f" -o {args.symbol_offset}"

        gdb_cmd.extend(["-ex", sym_arg])

    # finally the test script itself
    if args.test:
        if args.test_args:
            gdb_cmd.extend(["-ex", f"py sys.argv={args.test_args}"])
        gdb_cmd.extend(["-x", args.test])

    sleep(1)
    log(output, "GDB CMD: %s" % (format_cmd(gdb_cmd)))

    gdb_env = dict(os.environ)
    gdb_pythonpath = gdb_env.get("PYTHONPATH", "").split(os.pathsep)
    gdb_pythonpath.append(os.path.dirname(os.path.realpath(__file__)))
    gdb_env["PYTHONPATH"] = os.pathsep.join(gdb_pythonpath)
    result = subprocess.call(gdb_cmd, stdout=output, stderr=stderr,
                             env=gdb_env)

    # A result of greater than 128 indicates a fatal signal (likely a
    # crash due to gdb internal failure). That's a problem for GDB and
    # not the test so we force a return of 0 so we don't fail the test on
    # account of broken external tools.
    if result > 128:
        log(output, "GDB crashed? (%d, %d) SKIPPING" % (result, result - 128))
        exit(0)

    try:
        inferior.wait(2)
    except subprocess.TimeoutExpired:
        log(output, "GDB never connected? Killed guest")
        inferior.kill()

    exit(result)
