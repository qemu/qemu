# SPDX-License-Identifier: GPL-2.0-or-later
#
# Generic Kernel GDB script to verify functionality:
#  - Single-stepping
#  - Hardware breakpoints
#  - Software breakpoints
#  - Watchpoints
#
# It relies on fairly common kernel points to break at and symbols for
# the kernel. Address space randomisation (kaslr) needs to be disabled.
#
# Copyright (c) 2026 Linaro Limited
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# This is launched by the tuxrun functional tests via tests/guest-debug/run-test.py

import gdb
from test_gdbstub import main, report

def parse_registers():
    """Parse 'info registers' into a name -> value dictionary."""
    regs = {}
    for line in gdb.execute("info registers", to_string=True).splitlines():
        parts = line.split()
        if len(parts) >= 2:
            regs[parts[0]] = parts[1]
    return regs


def check_step():
    "Step an instruction, check it moved."
    start_pc = gdb.parse_and_eval('$pc')
    gdb.execute("si")
    end_pc = gdb.parse_and_eval('$pc')

    return not (start_pc == end_pc)


def check_reg_updates(regs_start):
    regs_end = parse_registers()

    changed = [k for k in regs_start
               if k in regs_end and regs_start[k] != regs_end[k]]

    report(len(changed) > 0, f"{len(changed)} registers changed.")


def check_break(sym_name):
    "Setup breakpoint, continue and check we stopped."
    bp = gdb.Breakpoint(sym_name)
    gdb.execute("c")
    res = bp.hit_count == 1
    bp.delete()

    return res


def check_hbreak(sym_name):
    "Setup hardware breakpoint, continue and check we stopped."
    bp = gdb.Breakpoint(sym_name, type = gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("c")
    res = bp.hit_count == 1
    bp.delete()

    return res


class WatchPoint(gdb.Breakpoint):

    def __init__(self, sym_name, type):
        super(WatchPoint, self).__init__(sym_name, gdb.BP_WATCHPOINT, type)

    def stop(self):
        end_pc = gdb.parse_and_eval('$pc')
        print("HIT WP @ %s" % (end_pc))
        return True


def do_one_watch(sym, wtype, text):

    wp = WatchPoint(sym, wtype)
    gdb.execute("c")
    report_str = "%s for %s" % (text, sym)

    if wp.hit_count > 0:
        report(True, report_str)
    else:
        report(False, report_str)

    wp.delete()


def check_watches(sym_name):
    "Watch a symbol for any access."

    # s390 only supports write watchpoints
    if "s390" not in gdb.selected_frame().architecture().name():
        # Should hit for any read
        do_one_watch(sym_name, gdb.WP_ACCESS, "awatch")

        # Again should hit for reads
        do_one_watch(sym_name, gdb.WP_READ, "rwatch")

    # Finally when it is written
    do_one_watch(sym_name, gdb.WP_WRITE, "watch")


class CatchBreakpoint(gdb.Breakpoint):
    def __init__(self, sym_name):
        super(CatchBreakpoint, self).__init__(sym_name, type = gdb.BP_BREAKPOINT)
        self.sym, ok = gdb.lookup_symbol(sym_name)

    def stop(self):
        end_pc = gdb.parse_and_eval('$pc')
        print("CB: %s == %s" % (end_pc, self.sym.value()))
        if end_pc == self.sym.value():
            report(False, "Hit final catchpoint")


def run_test():
    "Run through the tests one by one"

    # get the initial register state
    regs_start = parse_registers()

    print("Checking we can step the first few instructions")
    step_ok = 0
    for i in range(3):
        if check_step():
            step_ok += 1

    report(step_ok == 3, "single step in boot code")

    # check some registers changed
    print("Checking register access works")
    check_reg_updates(regs_start)

    print("Checking HW breakpoint works")
    break_ok = check_hbreak("kernel_init")
    report(break_ok, "hbreak @ kernel_init")

    # Can't set this up until we are in the kernel proper
    # if we make it to run_init_process we've over-run and
    # one of the tests failed

    print("Setup catch-all for run_init_process")
    cbp = CatchBreakpoint("run_init_process")

    # We should see wait_for_completion called right at the start of
    # kernel_init
    print("Checking Normal breakpoint works")
    break_ok = check_break("wait_for_completion")
    report(break_ok, "break @ wait_for_completion")

    print("Checking watchpoint works")
    check_watches("system_state")

    # check we didn't hit the end
    report(cbp.hit_count == 0, "CatchBreakpoint not hit")


main(run_test)
