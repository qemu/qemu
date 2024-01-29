from __future__ import print_function
#
# Test auxiliary vector is loaded via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report


def run_test():
    "Run through the tests one by one"

    auxv = gdb.execute("info auxv", False, True)
    report(isinstance(auxv, str), "Fetched auxv from inferior")
    report(auxv.find("sha1"), "Found test binary name in auxv")


main(run_test)
