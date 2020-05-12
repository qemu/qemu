# -*- coding: utf-8 -*-
# GDB debugging support
#
# Copyright 2017 Linaro Ltd
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

# 'qemu timers' -- display the current timerlists

import gdb

class TimersCommand(gdb.Command):
    '''Display the current QEMU timers'''

    def __init__(self):
        'Register the class as a gdb command'
        gdb.Command.__init__(self, 'qemu timers', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def dump_timers(self, timer):
        "Follow a timer and recursively dump each one in the list."
        # timer should be of type QemuTimer
        gdb.write("    timer %s/%s (cb:%s,opq:%s)\n" % (
            timer['expire_time'],
            timer['scale'],
            timer['cb'],
            timer['opaque']))

        if int(timer['next']) > 0:
            self.dump_timers(timer['next'])


    def process_timerlist(self, tlist, ttype):
        gdb.write("Processing %s timers\n" % (ttype))
        gdb.write("  clock %s is enabled:%s, last:%s\n" % (
            tlist['clock']['type'],
            tlist['clock']['enabled'],
            tlist['clock']['last']))
        if int(tlist['active_timers']) > 0:
            self.dump_timers(tlist['active_timers'])


    def invoke(self, arg, from_tty):
        'Run the command'
        main_timers = gdb.parse_and_eval("main_loop_tlg")

        # This will break if QEMUClockType in timer.h is redfined
        self.process_timerlist(main_timers['tl'][0], "Realtime")
        self.process_timerlist(main_timers['tl'][1], "Virtual")
        self.process_timerlist(main_timers['tl'][2], "Host")
        self.process_timerlist(main_timers['tl'][3], "Virtual RT")
