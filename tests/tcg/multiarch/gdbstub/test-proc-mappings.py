"""Test that gdbstub has access to proc mappings.

This runs as a sourced script (via -x, via run-test.py)."""
from __future__ import print_function
import gdb
from test_gdbstub import main, report


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


main(run_test)
