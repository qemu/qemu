"""Helper functions for gdbstub testing

"""
from __future__ import print_function
import gdb
import os
import sys
import traceback

fail_count = 0


def report(cond, msg):
    """Report success/fail of a test"""
    if cond:
        print("PASS: {}".format(msg))
    else:
        print("FAIL: {}".format(msg))
        global fail_count
        fail_count += 1


def main(test, expected_arch=None):
    """Run a test function

    This runs as the script it sourced (via -x, via run-test.py)."""
    try:
        inferior = gdb.selected_inferior()
        arch = inferior.architecture()
        print("ATTACHED: {}".format(arch.name()))
        if expected_arch is not None:
            report(arch.name() == expected_arch,
                   "connected to {}".format(expected_arch))
    except (gdb.error, AttributeError):
        print("SKIP: not connected")
        exit(0)

    if gdb.parse_and_eval("$pc") == 0:
        print("SKIP: PC not set")
        exit(0)

    try:
        test()
    except:
        print("GDB Exception:")
        traceback.print_exc(file=sys.stdout)
        global fail_count
        fail_count += 1
        if "QEMU_TEST_INTERACTIVE" in os.environ:
            import code
            code.InteractiveConsole(locals=globals()).interact()
        raise

    try:
        gdb.execute("kill")
    except gdb.error:
        pass

    print("All tests complete: {} failures".format(fail_count))
    exit(fail_count)
