"""Test that GDB can access PROT_NONE pages.

This runs as a sourced script (via -x, via run-test.py).

SPDX-License-Identifier: GPL-2.0-or-later
"""
import ctypes
import ctypes.util
import mmap
import os
from test_gdbstub import gdb_exit, main, report


def probe_proc_self_mem():
    buf = ctypes.create_string_buffer(b'aaa')
    try:
        with open("/proc/self/mem", "rb") as fp:
            fp.seek(ctypes.addressof(buf))
            return fp.read(3) == b'aaa'
    except OSError:
        return False

def probe_proc_self_mem_access_prot_none():
    libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
    libc.mmap.restype = ctypes.c_void_p
    libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                          ctypes.c_int, ctypes.c_int, ctypes.c_long]
    size = os.sysconf("SC_PAGESIZE")
    # mmap a PROT_NONE page
    PROT_NONE = 0
    addr = libc.mmap(None, size, PROT_NONE,
                     mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS, -1, 0)
    assert addr != ctypes.c_void_p(-1).value
    fd = os.open("/proc/self/mem", os.O_RDWR)
    try:
        # read it through /proc/self/mem
        # this is the fallback in cpu_memory_rw_debug
        data = os.pread(fd, size, addr)
    except Exception as e:
        print("/proc/self/mem pread error: " + str(e))
        return False

    try:
        # write it through /proc/self/mem
        # this is the fallback in cpu_memory_rw_debug
        os.pwrite(fd, data, addr)
    except Exception as e:
        print("/proc/self/mem pwrite error: " + str(e))
        return False

    return True


def run_test():
    """Run through the tests one by one"""
    if not probe_proc_self_mem():
        print("----------------------------------")
        print("SKIP: /proc/self/mem is not usable")
        print("----------------------------------")
        gdb_exit(77)
    if not probe_proc_self_mem_access_prot_none():
        print("----------------------------------")
        print("SKIP: /proc/self/mem can't be used to access PROT_NONE page")
        print("----------------------------------")
        gdb_exit(77)
    gdb.Breakpoint("break_here")
    gdb.execute("continue")
    val = gdb.parse_and_eval("*(char[2] *)q").string()
    report(val == "42", "{} == 42".format(val))
    gdb.execute("set *(char[3] *)q = \"24\"")
    gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
