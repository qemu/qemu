from __future__ import print_function
#
# Test the SVE registers are visable and changeable via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
import sys

MAGIC = 0xDEADBEEF

failcount = 0

def report(cond, msg):
    "Report success/fail of test"
    if cond:
        print ("PASS: %s" % (msg))
    else:
        print ("FAIL: %s" % (msg))
        global failcount
        failcount += 1

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

#
# This runs as the script it sourced (via -x, via run-test.py)
#
try:
    inferior = gdb.selected_inferior()
    if inferior.was_attached == False:
        print("SKIPPING (failed to attach)", file=sys.stderr)
        exit(0)
    arch = inferior.architecture()
    report(arch.name() == "aarch64", "connected to aarch64")
except (gdb.error, AttributeError):
    print("SKIPPING (not connected)", file=sys.stderr)
    exit(0)

try:
    # These are not very useful in scripts
    gdb.execute("set pagination off")
    gdb.execute("set confirm off")

    # Run the actual tests
    run_test()
except:
    print ("GDB Exception: %s" % (sys.exc_info()[0]))
    failcount += 1

print("All tests complete: %d failures" % failcount)

exit(failcount)
