"""Test single-stepping SVC.

This runs as a sourced script (via -x, via run-test.py)."""
import gdb
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    report("lghi\t" in gdb.execute("x/i $pc", False, True), "insn #1")
    gdb.execute("si")
    report("larl\t" in gdb.execute("x/i $pc", False, True), "insn #2")
    gdb.execute("si")
    report("lgrl\t" in gdb.execute("x/i $pc", False, True), "insn #3")
    gdb.execute("si")
    report("svc\t" in gdb.execute("x/i $pc", False, True), "insn #4")
    gdb.execute("si")
    report("xgr\t" in gdb.execute("x/i $pc", False, True), "insn #5")
    gdb.execute("si")
    report("svc\t" in gdb.execute("x/i $pc", False, True), "insn #6")
    gdb.execute("si")


main(run_test)
