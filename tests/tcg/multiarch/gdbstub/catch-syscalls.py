"""Test GDB syscall catchpoints.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def check_state(expected):
    """Check the catch_syscalls_state value"""
    actual = gdb.parse_and_eval("catch_syscalls_state").string()
    report(actual == expected, "{} == {}".format(actual, expected))


def run_test():
    """Run through the tests one by one"""
    gdb.Breakpoint("main")
    gdb.execute("continue")

    # Check that GDB stops for pipe2/read calls/returns, but not for write.
    gdb.execute("delete")
    try:
        gdb.execute("catch syscall pipe2 read")
    except gdb.error as exc:
        exc_str = str(exc)
        if "not supported on this architecture" in exc_str:
            print("SKIP: {}".format(exc_str))
            return
        raise
    for _ in range(2):
        gdb.execute("continue")
        check_state("pipe2")
    for _ in range(2):
        gdb.execute("continue")
        check_state("read")

    # Check that deletion works.
    gdb.execute("delete")
    gdb.Breakpoint("end_of_main")
    gdb.execute("continue")
    check_state("end")

    # Check that catch-all works (libc should at least call exit).
    gdb.execute("delete")
    gdb.execute("catch syscall")
    gdb.execute("continue")
    gdb.execute("delete")
    gdb.execute("continue")

    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
