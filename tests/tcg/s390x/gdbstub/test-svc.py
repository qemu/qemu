"""Test single-stepping SVC.

This runs as a sourced script (via -x, via run-test.py)."""
from __future__ import print_function
import gdb
import sys


n_failures = 0


def report(cond, msg):
    """Report success/fail of a test"""
    if cond:
        print("PASS: {}".format(msg))
    else:
        print("FAIL: {}".format(msg))
        global n_failures
        n_failures += 1


def run_test():
    """Run through the tests one by one"""
    report("lghi\t" in gdb.execute("x/i $pc", False, True), "insn #1")
    gdb.execute("si")
    report("larl\t" in gdb.execute("x/i $pc", False, True), "insn #2")
    gdb.execute("si")
    report("lgrl\t" in gdb.execute("x/i $pc", False, True), "insn #3")
    gdb.execute("si")
    report("svc\t" in gdb.execute("x/i $pc", False, True), "insn #4")
    gdb.execute("si")
    report("xgr\t" in gdb.execute("x/i $pc", False, True), "insn #5")
    gdb.execute("si")
    report("svc\t" in gdb.execute("x/i $pc", False, True), "insn #6")
    gdb.execute("si")


def main():
    """Prepare the environment and run through the tests"""
    try:
        inferior = gdb.selected_inferior()
        print("ATTACHED: {}".format(inferior.architecture().name()))
    except (gdb.error, AttributeError):
        print("SKIPPING (not connected)")
        exit(0)

    if gdb.parse_and_eval('$pc') == 0:
        print("SKIP: PC not set")
        exit(0)

    try:
        # Run the actual tests
        run_test()
    except gdb.error:
        report(False, "GDB Exception: {}".format(sys.exc_info()[0]))
    print("All tests complete: %d failures" % n_failures)
    exit(n_failures)


main()
