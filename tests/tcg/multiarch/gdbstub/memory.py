from __future__ import print_function
#
# Test some of the system debug features with the multiarch memory
# test. It is a port of the original vmlinux focused test case but
# using the "memory" test instead.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
import sys
from test_gdbstub import main, report


def check_step():
    "Step an instruction, check it moved."
    start_pc = gdb.parse_and_eval('$pc')
    gdb.execute("si")
    end_pc = gdb.parse_and_eval('$pc')

    return not (start_pc == end_pc)


#
# Currently it's hard to create a hbreak with the pure python API and
# manually matching PC to symbol address is a bit flaky thanks to
# function prologues. However internally QEMU's gdbstub treats them
# the same as normal breakpoints so it will do for now.
#
def check_break(sym_name):
    "Setup breakpoint, continue and check we stopped."
    sym, ok = gdb.lookup_symbol(sym_name)
    bp = gdb.Breakpoint(sym_name, gdb.BP_BREAKPOINT)

    gdb.execute("c")

    # hopefully we came back
    end_pc = gdb.parse_and_eval('$pc')
    report(bp.hit_count == 1,
           "break @ %s (%s %d hits)" % (end_pc, sym.value(), bp.hit_count))

    bp.delete()


def do_one_watch(sym, wtype, text):

    wp = gdb.Breakpoint(sym, gdb.BP_WATCHPOINT, wtype)
    gdb.execute("c")
    report_str = "%s for %s" % (text, sym)

    if wp.hit_count > 0:
        report(True, report_str)
        wp.delete()
    else:
        report(False, report_str)


def check_watches(sym_name):
    "Watch a symbol for any access."

    # Should hit for any read
    do_one_watch(sym_name, gdb.WP_ACCESS, "awatch")

    # Again should hit for reads
    do_one_watch(sym_name, gdb.WP_READ, "rwatch")

    # Finally when it is written
    do_one_watch(sym_name, gdb.WP_WRITE, "watch")


def run_test():
    "Run through the tests one by one"

    print("Checking we can step the first few instructions")
    step_ok = 0
    for i in range(3):
        if check_step():
            step_ok += 1

    report(step_ok == 3, "single step in boot code")

    # If we get here we have missed some of the other breakpoints.
    print("Setup catch-all for _exit")
    cbp = gdb.Breakpoint("_exit", gdb.BP_BREAKPOINT)

    check_break("main")
    check_watches("test_data[128]")

    report(cbp.hit_count == 0, "didn't reach backstop")


main(run_test)
