from __future__ import print_function
#
# Test auxiliary vector is loaded via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
import sys

failcount = 0

def report(cond, msg):
    "Report success/fail of test"
    if cond:
        print ("PASS: %s" % (msg))
    else:
        print ("FAIL: %s" % (msg))
        global failcount
        failcount += 1

def run_test():
    "Run through the tests one by one"

    auxv = gdb.execute("info auxv", False, True)
    report(isinstance(auxv, str), "Fetched auxv from inferior")
    report(auxv.find("sha1"), "Found test binary name in auxv")

#
# This runs as the script it sourced (via -x, via run-test.py)
#
try:
    inferior = gdb.selected_inferior()
    arch = inferior.architecture()
    print("ATTACHED: %s" % arch.name())
except (gdb.error, AttributeError):
    print("SKIPPING (not connected)", file=sys.stderr)
    exit(0)

if gdb.parse_and_eval('$pc') == 0:
    print("SKIP: PC not set")
    exit(0)

try:
    # These are not very useful in scripts
    gdb.execute("set pagination off")
    gdb.execute("set confirm off")

    # Run the actual tests
    run_test()
except (gdb.error):
    print ("GDB Exception: %s" % (sys.exc_info()[0]))
    failcount += 1
    pass

print("All tests complete: %d failures" % failcount)
exit(failcount)
