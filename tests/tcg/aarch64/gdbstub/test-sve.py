from __future__ import print_function
#
# Test the SVE registers are visible and changeable via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report

MAGIC = 0xDEADBEEF


def run_test():
    "Run through the tests one by one"

    gdb.execute("info registers")
    report(True, "info registers")

    gdb.execute("info registers vector")
    report(True, "info registers vector")

    # Now all the zregs
    frame = gdb.selected_frame()
    for i in range(0, 32):
        rname = "z%d" % (i)
        zreg = frame.read_register(rname)
        report(True, "Reading %s" % rname)
        for j in range(0, 4):
            cmd = "set $%s.q.u[%d] = 0x%x" % (rname, j, MAGIC)
            gdb.execute(cmd)
            report(True, "%s" % cmd)
        for j in range(0, 4):
            reg = "$%s.q.u[%d]" % (rname, j)
            v = gdb.parse_and_eval(reg)
            report(str(v.type) == "uint128_t", "size of %s" % (reg))
        for j in range(0, 8):
            cmd = "set $%s.d.u[%d] = 0x%x" % (rname, j, MAGIC)
            gdb.execute(cmd)
            report(True, "%s" % cmd)
        for j in range(0, 8):
            reg = "$%s.d.u[%d]" % (rname, j)
            v = gdb.parse_and_eval(reg)
            report(str(v.type) == "uint64_t", "size of %s" % (reg))
            report(int(v) == MAGIC, "%s is 0x%x" % (reg, MAGIC))


main(run_test, expected_arch="aarch64")
