from __future__ import print_function
#
# A very simple smoke test for debugging the SHA1 userspace test on
# each target.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report


initial_vlen = 0


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

    # Check step and inspect values. We do a double next after the
    # breakpoint as depending on the version of gdb we may step the
    # preamble and not the first actual line of source.
    gdb.execute("next")
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


main(run_test)
