#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Analyse lock events and compute statistics
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#

import simpletrace
import argparse
import numpy as np

class MutexAnalyser(simpletrace.Analyzer):
    "A simpletrace Analyser for checking locks."

    def __init__(self):
        self.locks = 0
        self.locked = 0
        self.unlocks = 0
        self.mutex_records = {}

    def _get_mutex(self, mutex):
        if not mutex in self.mutex_records:
            self.mutex_records[mutex] = {"locks": 0,
                                         "lock_time": 0,
                                         "acquire_times": [],
                                         "locked": 0,
                                         "locked_time": 0,
                                         "held_times": [],
                                         "unlocked": 0}

        return self.mutex_records[mutex]

    def qemu_mutex_lock(self, timestamp, mutex, filename, line):
        self.locks += 1
        rec = self._get_mutex(mutex)
        rec["locks"] += 1
        rec["lock_time"] = timestamp[0]
        rec["lock_loc"] = (filename, line)

    def qemu_mutex_locked(self, timestamp, mutex, filename, line):
        self.locked += 1
        rec = self._get_mutex(mutex)
        rec["locked"] += 1
        rec["locked_time"] = timestamp[0]
        acquire_time = rec["locked_time"] - rec["lock_time"]
        rec["locked_loc"] = (filename, line)
        rec["acquire_times"].append(acquire_time)

    def qemu_mutex_unlock(self, timestamp, mutex, filename, line):
        self.unlocks += 1
        rec = self._get_mutex(mutex)
        rec["unlocked"] += 1
        held_time = timestamp[0] - rec["locked_time"]
        rec["held_times"].append(held_time)
        rec["unlock_loc"] = (filename, line)


def get_args():
    "Grab options"
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", "-o", type=str, help="Render plot to file")
    parser.add_argument("events", type=str, help='trace file read from')
    parser.add_argument("tracefile", type=str, help='trace file read from')
    return parser.parse_args()

if __name__ == '__main__':
    args = get_args()

    # Gather data from the trace
    analyser = MutexAnalyser()
    simpletrace.process(args.events, args.tracefile, analyser)

    print ("Total locks: %d, locked: %d, unlocked: %d" %
           (analyser.locks, analyser.locked, analyser.unlocks))

    # Now dump the individual lock stats
    for key, val in sorted(analyser.mutex_records.items(),
                           key=lambda k_v: k_v[1]["locks"]):
        print ("Lock: %#x locks: %d, locked: %d, unlocked: %d" %
               (key, val["locks"], val["locked"], val["unlocked"]))

        acquire_times = np.array(val["acquire_times"])
        if len(acquire_times) > 0:
            print ("  Acquire Time: min:%d median:%d avg:%.2f max:%d" %
                   (acquire_times.min(), np.median(acquire_times),
                    acquire_times.mean(), acquire_times.max()))

        held_times = np.array(val["held_times"])
        if len(held_times) > 0:
            print ("  Held Time: min:%d median:%d avg:%.2f max:%d" %
                   (held_times.min(), np.median(held_times),
                    held_times.mean(), held_times.max()))

        # Check if any locks still held
        if val["locks"] > val["locked"]:
            print ("  LOCK HELD (%s:%s)" % (val["locked_loc"]))
            print ("  BLOCKED   (%s:%s)" % (val["lock_loc"]))
