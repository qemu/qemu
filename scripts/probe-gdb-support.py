#!/usr/bin/env python3
# coding: utf-8
#
# Probe gdb for supported architectures.
#
# This is required to support testing of the gdbstub as its hard to
# handle errors gracefully during the test. Instead this script when
# passed a GDB binary will probe its architecture support and return a
# string of supported arches, stripped of guff.
#
# Copyright 2023 Linaro Ltd
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import re
from subprocess import check_output, STDOUT

# mappings from gdb arch to QEMU target
mappings = {
    "alpha" : "alpha",
    "aarch64" : ["aarch64", "aarch64_be"],
    "armv7": "arm",
    "armv8-a" : ["aarch64", "aarch64_be"],
    "avr" : "avr",
    "cris" : "cris",
    # no hexagon in upstream gdb
    "hppa1.0" : "hppa",
    "i386" : "i386",
    "i386:x86-64" : "x86_64",
    "Loongarch64" : "loongarch64",
    "m68k" : "m68k",
    "MicroBlaze" : "microblaze",
    "mips:isa64" : ["mips64", "mips64el"],
    "nios2" : "nios2",
    "or1k" : "or1k",
    "powerpc:common" : "ppc",
    "powerpc:common64" : ["ppc64", "ppc64le"],
    "riscv:rv32" : "riscv32",
    "riscv:rv64" : "riscv64",
    "s390:64-bit" : "s390x",
    "sh4" : ["sh4", "sh4eb"],
    "sparc": "sparc",
    "sparc:v8plus": "sparc32plus",
    "sparc:v9a" : "sparc64",
    # no tricore in upstream gdb
    "xtensa" : ["xtensa", "xtensaeb"]
}

def do_probe(gdb):
    gdb_out = check_output([gdb,
                            "-ex", "set architecture",
                            "-ex", "quit"], stderr=STDOUT)

    m = re.search(r"Valid arguments are (.*)",
                  gdb_out.decode("utf-8"))

    valid_arches = set()

    if m.group(1):
        for arch in m.group(1).split(", "):
            if arch in mappings:
                mapping = mappings[arch]
                if isinstance(mapping, str):
                    valid_arches.add(mapping)
                else:
                    for entry in mapping:
                        valid_arches.add(entry)

    return valid_arches

def main() -> None:
    parser = argparse.ArgumentParser(description='Probe GDB Architectures')
    parser.add_argument('gdb', help='Path to GDB binary.')

    args = parser.parse_args()

    supported = do_probe(args.gdb)

    print(" ".join(supported))

if __name__ == '__main__':
    main()
