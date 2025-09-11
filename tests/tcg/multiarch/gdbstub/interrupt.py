#
# Test some of the system debug features with the multiarch memory
# test. It is a port of the original vmlinux focused test case but
# using the "memory" test instead.
#
# This is launched via tests/guest-debug/run-test.py
#

import gdb
from test_gdbstub import gdb_exit, main, report


def check_interrupt(thread):
    """
    Check that, if thread is resumed, we go back to the same thread when the
    program gets interrupted.
    """

    # Switch to the thread we're going to be running the test in.
    print("thread ", thread.num)
    gdb.execute("thr %d" % thread.num)

    # Enter the loop() function on this thread.
    #
    # While there are cleaner ways to do this, we want to minimize the number of
    # side effects on the gdbstub's internal state, since those may mask bugs.
    # Ideally, there should be no difference between what we're doing here and
    # the program reaching the loop() function on its own.
    #
    # For this to be safe, we only need the prologue of loop() to not have
    # instructions that may have problems with what we're doing here. We don't
    # have to worry about anything else, as this function never returns.
    gdb.execute("set $pc = loop")

    # Continue and then interrupt the task.
    gdb.post_event(lambda: gdb.execute("interrupt"))
    gdb.execute("c")

    # Check whether the thread we're in after the interruption is the same we
    # ran continue from.
    return (thread.num == gdb.selected_thread().num)


def run_test():
    """
    Test if interrupting the code always lands us on the same thread when
    running with scheduler-lock enabled.
    """
    if len(gdb.selected_inferior().threads()) == 1:
        print("SKIP: set to run on a single thread")
        gdb_exit(0)

    gdb.execute("set scheduler-locking on")
    for thread in gdb.selected_inferior().threads():
        report(check_interrupt(thread),
               "thread %d resumes correctly on interrupt" % thread.num)


main(run_test)
