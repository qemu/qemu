from __future__ import print_function

#
# Test that signals and debugging mix well together on s390x.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
import sys

failcount = 0


def report(cond, msg):
    """Report success/fail of test"""
    if cond:
        print("PASS: %s" % (msg))
    else:
        print("FAIL: %s" % (msg))
        global failcount
        failcount += 1


def run_test():
    """Run through the tests one by one"""
    illegal_op = gdb.Breakpoint("illegal_op")
    stg = gdb.Breakpoint("stg")
    mvc_8 = gdb.Breakpoint("mvc_8")

    # Expect the following events:
    # 1x illegal_op breakpoint
    # 2x stg breakpoint, segv, breakpoint
    # 2x mvc_8 breakpoint, segv, breakpoint
    for _ in range(14):
        gdb.execute("c")
    report(illegal_op.hit_count == 1, "illegal_op.hit_count == 1")
    report(stg.hit_count == 4, "stg.hit_count == 4")
    report(mvc_8.hit_count == 4, "mvc_8.hit_count == 4")

    # The test must succeed.
    gdb.Breakpoint("_exit")
    gdb.execute("c")
    status = int(gdb.parse_and_eval("$r2"))
    report(status == 0, "status == 0");


#
# This runs as the script it sourced (via -x, via run-test.py)
#
try:
    inferior = gdb.selected_inferior()
    arch = inferior.architecture()
    print("ATTACHED: %s" % arch.name())
except (gdb.error, AttributeError):
    print("SKIPPING (not connected)", file=sys.stderr)
    exit(0)

if gdb.parse_and_eval("$pc") == 0:
    print("SKIP: PC not set")
    exit(0)

try:
    # These are not very useful in scripts
    gdb.execute("set pagination off")
    gdb.execute("set confirm off")

    # Run the actual tests
    run_test()
except (gdb.error):
    print("GDB Exception: %s" % (sys.exc_info()[0]))
    failcount += 1
    pass

print("All tests complete: %d failures" % failcount)
exit(failcount)
