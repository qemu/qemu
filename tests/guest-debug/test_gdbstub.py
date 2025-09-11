"""Helper functions for gdbstub testing

"""
import argparse
import gdb
import os
import sys
import traceback

fail_count = 0


def gdb_exit(status):
    gdb.execute(f"exit {status}")


class arg_parser(argparse.ArgumentParser):
    def exit(self, status=None, message=""):
        print("Wrong GDB script test argument! " + message)
        gdb_exit(1)


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
        gdb_exit(0)

    if gdb.parse_and_eval("$pc") == 0:
        print("SKIP: PC not set")
        gdb_exit(0)

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
    gdb_exit(fail_count)
