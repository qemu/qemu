from __future__ import print_function
#
# Test gdbstub Xfer:siginfo:read stub.
#
# The test runs a binary that causes a SIGSEGV and then looks for additional
# info about the signal through printing GDB's '$_siginfo' special variable,
# which sends a Xfer:siginfo:read query to the gdbstub.
#
# The binary causes a SIGSEGV at dereferencing a pointer with value 0xdeadbeef,
# so the test looks for and checks if this address is correctly reported by the
# gdbstub.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report

def run_test():
    "Run through the test"

    gdb.execute("continue", False, True)
    resp = gdb.execute("print/x $_siginfo", False, True)
    report(resp.find("si_addr = 0xdeadbeef"), "Found fault address.")

main(run_test)
