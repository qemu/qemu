"""Test GDB's follow-fork-mode child.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    gdb.execute("set follow-fork-mode child")
    # Check that the parent breakpoints are unset.
    gdb.execute("break break_after_fork")
    # Check that the parent syscall catchpoints are unset.
    # Skip this check on the architectures that don't have them.
    have_fork_syscall = False
    for fork_syscall in ("fork", "clone", "clone2", "clone3"):
        try:
            gdb.execute("catch syscall {}".format(fork_syscall))
        except gdb.error:
            pass
        else:
            have_fork_syscall = True
    gdb.execute("continue")
    for i in range(42):
        if have_fork_syscall:
            # syscall entry.
            if i % 2 == 0:
                # Check that the parent single-stepping is turned off.
                gdb.execute("si")
            else:
                gdb.execute("continue")
            # syscall exit.
            gdb.execute("continue")
        # break_after_fork()
        gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 42, "{} == 42".format(exitcode))


main(run_test)
