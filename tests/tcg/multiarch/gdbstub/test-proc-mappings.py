"""Test that gdbstub has access to proc mappings.

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
    try:
        mappings = gdb.execute("info proc mappings", False, True)
    except gdb.error as exc:
        exc_str = str(exc)
        if "Not supported on this target." in exc_str:
            # Detect failures due to an outstanding issue with how GDB handles
            # the x86_64 QEMU's target.xml, which does not contain the
            # definition of orig_rax. Skip the test in this case.
            print("SKIP: {}".format(exc_str))
            return
        raise
    report(isinstance(mappings, str), "Fetched the mappings from the inferior")
    # Broken with host page size > guest page size
    # report("/sha1" in mappings, "Found the test binary name in the mappings")


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
