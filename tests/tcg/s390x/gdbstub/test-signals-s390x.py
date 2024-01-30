from __future__ import print_function

#
# Test that signals and debugging mix well together on s390x.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report


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
    report(status == 0, "status == 0")


main(run_test)
