"""Test attaching GDB to a running process.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    try:
        phase = gdb.parse_and_eval("phase").string()
    except gdb.error:
        # Assume the guest did not reach main().
        phase = "start"

    if phase == "start":
        gdb.execute("break sigwait")
        gdb.execute("continue")
        phase = gdb.parse_and_eval("phase").string()
    report(phase == "sigwait", "{} == \"sigwait\"".format(phase))

    gdb.execute("signal SIGUSR1")

    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
