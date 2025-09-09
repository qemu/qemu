#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

#
# Test the SME registers are visible and changeable via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import argparse
import gdb
from test_gdbstub import main, report

MAGIC = 0x01020304
BASIC_ZA_TEST = 0
TILE_SLICE_TEST = 0


def run_test():
    """Run the requested test(s) for SME ZA gdbstub support"""

    if BASIC_ZA_TEST:
        run_basic_sme_za_gdbstub_support_test()
    if TILE_SLICE_TEST:
        run_basic_sme_za_tile_slice_gdbstub_support_test()


def run_basic_sme_za_gdbstub_support_test():
    """Test reads and writes to the SME ZA register at the byte level"""

    frame = gdb.selected_frame()
    rname = "za"
    za = frame.read_register(rname)
    report(True, "Reading %s" % rname)

    # Writing to the ZA register, byte by byte.
    for i in range(0, 16):
        for j in range(0, 16):
            cmd = "set $za[%d][%d] = 0x01" % (i, j)
            gdb.execute(cmd)
            report(True, "%s" % cmd)

    # Reading from the ZA register, byte by byte.
    for i in range(0, 16):
        for j in range(0, 16):
            reg = "$za[%d][%d]" % (i, j)
            v = gdb.parse_and_eval(reg)
            report(str(v.type) == "uint8_t", "size of %s" % (reg))
            report(v == 0x1, "%s is 0x%x" % (reg, 0x1))


def run_basic_sme_za_tile_slice_gdbstub_support_test():
    """Test reads and writes of SME ZA horizontal and vertical tile slices

    Test if SME ZA tile slices, both horizontal and vertical,
    can be correctly read and written to. The sizes to test
    are quadwords and doublewords.
    """

    sizes = {}
    sizes["q"] = "uint128_t"
    sizes["d"] = "uint64_t"

    # Accessing requested sizes of elements of ZA
    for size in sizes:

        # Accessing various ZA tiles
        for i in range(0, 4):

            # Accessing various horizontal slices for each ZA tile
            for j in range(0, 4):
                # Writing to various elements in each tile slice
                for k in range(0, 4):
                    cmd = "set $za%dh%c%d[%d] = 0x%x" % (i, size, j, k, MAGIC)
                    gdb.execute(cmd)
                    report(True, "%s" % cmd)

                # Reading from the written elements in each tile slice
                for k in range(0, 4):
                    reg = "$za%dh%c%d[%d]" % (i, size, j, k)
                    v = gdb.parse_and_eval(reg)
                    report(str(v.type) == sizes[size], "size of %s" % (reg))
                    report(v == MAGIC, "%s is 0x%x" % (reg, MAGIC))

            # Accessing various vertical slices for each ZA tile
            for j in range(0, 4):
                # Writing to various elements in each tile slice
                for k in range(0, 4):
                    cmd = "set $za%dv%c%d[%d] = 0x%x" % (i, size, j, k, MAGIC)
                    gdb.execute(cmd)
                    report(True, "%s" % cmd)

                # Reading from the written elements in each tile slice
                for k in range(0, 4):
                    reg = "$za%dv%c%d[%d]" % (i, size, j, k)
                    v = gdb.parse_and_eval(reg)
                    report(str(v.type) == sizes[size], "size of %s" % (reg))
                    report(v == MAGIC, "%s is 0x%x" % (reg, MAGIC))


parser = argparse.ArgumentParser(description="A gdbstub test for SME support")
parser.add_argument("--gdb_basic_za_test",
                    help="Enable test for basic SME ZA support",
                    action="store_true")
parser.add_argument("--gdb_tile_slice_test",
                    help="Enable test for ZA tile slice support",
                    action="store_true")
args = parser.parse_args()

if args.gdb_basic_za_test:
    BASIC_ZA_TEST = 1
if args.gdb_tile_slice_test:
    TILE_SLICE_TEST = 1

main(run_test, expected_arch="aarch64")
