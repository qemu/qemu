# GDB debugging support, TCG status
#
# Copyright 2016 Linaro Ltd
#
# Authors:
#  Alex Bennée <alex.bennee@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

# 'qemu tcg-lock-status' -- display the TCG lock status across threads

import gdb

class TCGLockStatusCommand(gdb.Command):
    '''Display TCG Execution Status'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu tcg-lock-status', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        gdb.write("Thread, BQL (iothread_mutex), Replay, Blocked?\n")
        for thread in gdb.inferiors()[0].threads():
            thread.switch()

            iothread = gdb.parse_and_eval("iothread_locked")
            replay = gdb.parse_and_eval("replay_locked")

            frame = gdb.selected_frame()
            if frame.name() == "__lll_lock_wait":
                frame.older().select()
                mutex = gdb.parse_and_eval("mutex")
                owner = gdb.parse_and_eval("mutex->__data.__owner")
                blocked = ("__lll_lock_wait waiting on %s from %d" %
                           (mutex, owner))
            else:
                blocked = "not blocked"

            gdb.write("%d/%d, %s, %s, %s\n" % (thread.num, thread.ptid[1],
                                               iothread, replay, blocked))
