#
# Test GDB memory-tag commands that exercise the stubs for the qIsAddressTagged,
# qMemTag, and QMemTag packets, which are used for manipulating allocation tags.
# Logical tags-related commands rely on local operations, hence don't exercise
# any stub and so are not used in this test.
#
# The test consists in breaking just after a tag is set in a specific memory
# chunk, and then using the GDB 'memory-tagging' subcommands to set/get tags in
# different memory locations and ranges in the MTE-enabled memory chunk.
#
# This is launched via tests/guest-debug/run-test.py
#


try:
    import gdb
except ModuleNotFoundError:
    from sys import exit
    exit("This script must be launched via tests/guest-debug/run-test.py!")
import re
from sys import argv
from test_gdbstub import arg_parser, main, report


PATTERN_0 = r"Memory tags for address 0x[0-9a-f]+ match \(0x[0-9a-f]+\)."
PATTERN_1 = r".*(0x[0-9a-f]+)"


def run_test():
    p = arg_parser(prog="test-mte.py", description="TCG MTE tests.")
    p.add_argument("--mode", help="Run test for QEMU system or user mode.",
                   required=True, choices=['system','user'])

    args = p.parse_args(args=argv)

    if args.mode == "system":
        # Break address: where to break before performing the tests
        # See mte.S for details about this label.
        ba = "main_end"
        # Tagged address: the start of the MTE-enabled memory chunk to be tested
        # 'tagged_addr' (x1) is a pointer to the MTE-enabled page. See mte.S.
        ta = "$x1"
    else: # mode="user"
        # Line 95 in mte-8.c
        ba = "95"
        # 'a' array. See mte-8.c
        ta = "a"

    gdb.execute(f"break {ba}", False, True)
    gdb.execute("continue", False, True)

    try:
        # Test if we can check correctly that the allocation tag for the address
        # in {ta} matches the logical tag in {ta}.
        co = gdb.execute(f"memory-tag check {ta}", False, True)
        tags_match = re.findall(PATTERN_0, co, re.MULTILINE)
        if tags_match:
            report(True, f"{tags_match[0]}")
        else:
            report(False, "Logical and allocation tags don't match!")

        # Test allocation tag 'set and print' commands. Commands on logical
        # tags rely on local operation and so don't exercise any stub.

        # Set the allocation tag for the first granule (16 bytes) of
        # address starting at {ta} address to a known value, i.e. 0x04.
        gdb.execute(f"memory-tag set-allocation-tag {ta} 1 04", False, True)

        # Then set the allocation tag for the second granule to a known
        # value, i.e. 0x06. This tests that contiguous tag granules are
        # set correctly and don't run over each other.
        gdb.execute(f"memory-tag set-allocation-tag {ta}+16 1 06", False, True)

        # Read the known values back and check if they remain the same.

        co = gdb.execute(f"memory-tag print-allocation-tag {ta}", False, True)
        first_tag = re.match(PATTERN_1, co)[1]

        co = gdb.execute(f"memory-tag print-allocation-tag {ta}+16", False, True)
        second_tag = re.match(PATTERN_1, co)[1]

        if first_tag == "0x4" and second_tag == "0x6":
            report(True, "Allocation tags are correctly set/printed.")
        else:
            report(False, "Can't set/print allocation tags!")

        # Now test fill pattern by setting a whole page with a pattern.
        gdb.execute(f"memory-tag set-allocation-tag {ta} 4096 0a0b", False, True)

        # And read back the tags of the last two granules in page so
        # we also test if the pattern is set correctly up to the end of
        # the page.
        co = gdb.execute(f"memory-tag print-allocation-tag {ta}+4096-32", False, True)
        tag = re.match(PATTERN_1, co)[1]

        co = gdb.execute(f"memory-tag print-allocation-tag {ta}+4096-16", False, True)
        last_tag = re.match(PATTERN_1, co)[1]

        if tag == "0xa" and last_tag == "0xb":
            report(True, "Fill pattern is ok.")
        else:
            report(False, "Fill pattern failed!")

    except gdb.error:
        # This usually happens because a GDB version that does not support
        # memory tagging was used to run the test.
        report(False, "'memory-tag' command failed!")


main(run_test, expected_arch="aarch64")
