"""Test that a breakpoint set after a cross-page chain is established is hit.

translator_use_goto_tb() lets a direct branch chain to another page in
user-only builds, which is only safe because a run with no gdbstub can never
acquire a breakpoint.  This runs with one, so the chaining must be off and
the breakpoint must still be reached.

This runs as a sourced script (via -x, via run-test.py).

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    gdb.Breakpoint("break_here")
    gdb.execute("continue")

    # The chain exists by now; put a breakpoint on the far side of it.
    target = int(gdb.parse_and_eval("(unsigned long)page_b_entry"))
    if target == 0:
        report(True, "no code emitters for this architecture, skipped")
        return
    gdb.execute("break *{}".format(target))
    gdb.execute("continue")

    pc = int(gdb.parse_and_eval("(unsigned long)$pc"))
    report(pc == target, "stopped at {:#x}, expected {:#x}".format(pc, target))

    gdb.execute("delete")
    gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
