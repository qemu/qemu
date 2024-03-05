"""Test GDB's follow-fork-mode parent.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    gdb.execute("set follow-fork-mode parent")
    gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
