"""Test that gdbstub has access to proc mappings.

This runs as a sourced script (via -x, via run-test.py)."""
import gdb
from test_gdbstub import gdb_exit, main, report


def run_test():
    """Run through the tests one by one"""
    if gdb.selected_inferior().architecture().name() == "m68k":
        # m68k GDB supports only GDB_OSABI_SVR4, but GDB_OSABI_LINUX is
        # required for the info proc support (see set_gdbarch_info_proc()).
        print("SKIP: m68k GDB does not support GDB_OSABI_LINUX")
        gdb_exit(0)
    mappings = gdb.execute("info proc mappings", False, True)
    report(isinstance(mappings, str), "Fetched the mappings from the inferior")
    # Broken with host page size > guest page size
    # report("/sha1" in mappings, "Found the test binary name in the mappings")


main(run_test)
