from __future__ import print_function
#
# A very simple smoke test for debugging the SHA1 userspace test on
# each target.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
import sys

initial_vlen = 0
failcount = 0

def report(cond, msg):
    "Report success/fail of test"
    if cond:
        print("PASS: %s" % (msg))
    else:
        print("FAIL: %s" % (msg))
        global failcount
        failcount += 1

def check_break(sym_name):
    "Setup breakpoint, continue and check we stopped."
    sym, ok = gdb.lookup_symbol(sym_name)
    bp = gdb.Breakpoint(sym_name)

    gdb.execute("c")

    # hopefully we came back
    end_pc = gdb.parse_and_eval('$pc')
    report(bp.hit_count == 1,
           "break @ %s (%s %d hits)" % (end_pc, sym.value(), bp.hit_count))

    bp.delete()

def run_test():
    "Run through the tests one by one"

    check_break("SHA1Init")

    # check step and inspect values
    gdb.execute("next")
    val_ctx = gdb.parse_and_eval("context->state[0]")
    exp_ctx = 0x67452301
    report(int(val_ctx) == exp_ctx, "context->state[0] == %x" % exp_ctx);

    gdb.execute("next")
    val_ctx = gdb.parse_and_eval("context->state[1]")
    exp_ctx = 0xEFCDAB89
    report(int(val_ctx) == exp_ctx, "context->state[1] == %x" % exp_ctx);

    # finally check we don't barf inspecting registers
    gdb.execute("info registers")

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
