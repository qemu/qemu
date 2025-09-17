#
# Test auxiliary vector is loaded via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import main, report


def run_test():
    "Run through the tests one by one"

    sym, ok = gdb.lookup_symbol("thread1_func")
    gdb.execute("b thread1_func")
    gdb.execute("c")

    frame = gdb.selected_frame()
    report(str(frame.function()) == "thread1_func", "break @ %s"%frame)


main(run_test)
