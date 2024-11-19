"""Test that GDB can access PROT_NONE pages.

This runs as a sourced script (via -x, via run-test.py).

SPDX-License-Identifier: GPL-2.0-or-later
"""
import ctypes
from test_gdbstub import gdb_exit, main, report


def probe_proc_self_mem():
    buf = ctypes.create_string_buffer(b'aaa')
    try:
        with open("/proc/self/mem", "rb") as fp:
            fp.seek(ctypes.addressof(buf))
            return fp.read(3) == b'aaa'
    except OSError:
        return False


def run_test():
    """Run through the tests one by one"""
    if not probe_proc_self_mem():
        print("SKIP: /proc/self/mem is not usable")
        gdb_exit(0)
    gdb.Breakpoint("break_here")
    gdb.execute("continue")
    val = gdb.parse_and_eval("*(char[2] *)q").string()
    report(val == "42", "{} == 42".format(val))
    gdb.execute("set *(char[3] *)q = \"24\"")
    gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
