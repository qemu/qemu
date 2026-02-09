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

    def _format_expire_time(self, expire_time, scale):
        "Return human-readable expiry time (ns) with scale info."
        secs = expire_time / 1e9

        # Select unit and compute value
        if secs < 1:
            val, unit = secs * 1000, "ms"
        elif secs < 60:
            val, unit = secs, "s"
        elif secs < 3600:
            val, unit = secs / 60, "min"
        elif secs < 86400:
            val, unit = secs / 3600, "hrs"
        else:
            val, unit = secs / 86400, "days"

        scale_map = {1: "ns", 1000: "us", 1000000: "ms",
                     1000000000: "s"}
        scale_str = scale_map.get(scale, f"scale={scale}")
        return f"{val:.2f} {unit} [{scale_str}]"

    def _format_attribute(self, attr):
        "Given QEMUTimer attributes value, return a human-readable string"

        # From include/qemu/timer.h
        if attr == 0:
            value = 'NONE'
        elif attr == 1 << 0:
            value = 'ATTR_EXTERNAL'
        elif attr == int(0xffffffff):
            value = 'ATTR_ALL'
        else:
            value = 'UNKNOWN'
        return f'{attr} <{value}>'

    def dump_timers(self, timer):
        "Follow a timer and recursively dump each one in the list."
        # timer should be of type QemuTimer
        scale = int(timer['scale'])
        expire_time = int(timer['expire_time'])
        attributes = int(timer['attributes'])

        time_str = self._format_expire_time(expire_time, scale)
        attr_str = self._format_attribute(attributes)

        gdb.write(f"    timer at {time_str} (attr:{attr_str}, "
                  f"cb:{timer['cb']}, opq:{timer['opaque']})\n")

        if int(timer['next']) > 0:
            self.dump_timers(timer['next'])


    def process_timerlist(self, tlist, ttype):
        gdb.write("Processing %s timers\n" % (ttype))
        gdb.write("  clock %s is enabled:%s\n" % (
            tlist['clock']['type'],
            tlist['clock']['enabled']))
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
