from __future__ import print_function
#
# Test GDB memory-tag commands that exercise the stubs for the qIsAddressTagged,
# qMemTag, and QMemTag packets. Logical tag-only commands rely on local
# operations, hence don't exercise any stub.
#
# The test consists in breaking just after a atag() call (which sets the
# allocation tag -- see mte-8.c for details) and setting/getting tags in
# different memory locations and ranges starting at the address of the array
# 'a'.
#
# This is launched via tests/guest-debug/run-test.py
#


import gdb
import re
from test_gdbstub import main, report


PATTERN_0 = "Memory tags for address 0x[0-9a-f]+ match \(0x[0-9a-f]+\)."
PATTERN_1 = ".*(0x[0-9a-f]+)"


def run_test():
    gdb.execute("break 95", False, True)
    gdb.execute("continue", False, True)
    try:
        # Test if we can check correctly that the allocation tag for
        # array 'a' matches the logical tag after atag() is called.
        co = gdb.execute("memory-tag check a", False, True)
        tags_match = re.findall(PATTERN_0, co, re.MULTILINE)
        if tags_match:
            report(True, f"{tags_match[0]}")
        else:
            report(False, "Logical and allocation tags don't match!")

        # Test allocation tag 'set and print' commands. Commands on logical
        # tags rely on local operation and so don't exercise any stub.

        # Set the allocation tag for the first granule (16 bytes) of
        # address starting at 'a' address to a known value, i.e. 0x04.
        gdb.execute("memory-tag set-allocation-tag a 1 04", False, True)

        # Then set the allocation tag for the second granule to a known
        # value, i.e. 0x06. This tests that contiguous tag granules are
        # set correct and don't run over each other.
        gdb.execute("memory-tag set-allocation-tag a+16 1 06", False, True)

        # Read the known values back and check if they remain the same.

        co = gdb.execute("memory-tag print-allocation-tag a", False, True)
        first_tag = re.match(PATTERN_1, co)[1]

        co = gdb.execute("memory-tag print-allocation-tag a+16", False, True)
        second_tag = re.match(PATTERN_1, co)[1]

        if first_tag == "0x4" and second_tag == "0x6":
            report(True, "Allocation tags are correctly set/printed.")
        else:
            report(False, "Can't set/print allocation tags!")

        # Now test fill pattern by setting a whole page with a pattern.
        gdb.execute("memory-tag set-allocation-tag a 4096 0a0b", False, True)

        # And read back the tags of the last two granules in page so
        # we also test if the pattern is set correctly up to the end of
        # the page.
        co = gdb.execute("memory-tag print-allocation-tag a+4096-32", False, True)
        tag = re.match(PATTERN_1, co)[1]

        co = gdb.execute("memory-tag print-allocation-tag a+4096-16", False, True)
        last_tag = re.match(PATTERN_1, co)[1]

        if tag == "0xa" and last_tag == "0xb":
            report(True, "Fill pattern is ok.")
        else:
            report(False, "Fill pattern failed!")

    except gdb.error:
        # This usually happens because a GDB version that does not
        # support memory tagging was used to run the test.
        report(False, "'memory-tag' command failed!")


main(run_test, expected_arch="aarch64")
