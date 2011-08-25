#!/usr/bin/env python
#
# Pretty-printer for simple trace backend binary trace files
#
# Copyright IBM, Corp. 2010
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# For help see docs/tracing.txt

import struct
import re
import inspect

header_event_id = 0xffffffffffffffff
header_magic    = 0xf2b177cb0aa429b4
header_version  = 0
dropped_event_id = 0xfffffffffffffffe

trace_fmt = '=QQQQQQQQ'
trace_len = struct.calcsize(trace_fmt)
event_re  = re.compile(r'(disable\s+)?([a-zA-Z0-9_]+)\(([^)]*)\).*')

def parse_events(fobj):
    """Parse a trace-events file into {event_num: (name, arg1, ...)}."""

    def get_argnames(args):
        """Extract argument names from a parameter list."""
        return tuple(arg.split()[-1].lstrip('*') for arg in args.split(','))

    events = {dropped_event_id: ('dropped', 'count')}
    event_num = 0
    for line in fobj:
        m = event_re.match(line.strip())
        if m is None:
            continue

        disable, name, args = m.groups()
        events[event_num] = (name,) + get_argnames(args)
        event_num += 1
    return events

def read_record(fobj):
    """Deserialize a trace record from a file into a tuple (event_num, timestamp, arg1, ..., arg6)."""
    s = fobj.read(trace_len)
    if len(s) != trace_len:
        return None
    return struct.unpack(trace_fmt, s)

def read_trace_file(fobj):
    """Deserialize trace records from a file, yielding record tuples (event_num, timestamp, arg1, ..., arg6)."""
    header = read_record(fobj)
    if header is None or \
       header[0] != header_event_id or \
       header[1] != header_magic or \
       header[2] != header_version:
        raise ValueError('not a trace file or incompatible version')

    while True:
        rec = read_record(fobj)
        if rec is None:
            break

        yield rec

class Analyzer(object):
    """A trace file analyzer which processes trace records.

    An analyzer can be passed to run() or process().  The begin() method is
    invoked, then each trace record is processed, and finally the end() method
    is invoked.

    If a method matching a trace event name exists, it is invoked to process
    that trace record.  Otherwise the catchall() method is invoked."""

    def begin(self):
        """Called at the start of the trace."""
        pass

    def catchall(self, event, rec):
        """Called if no specific method for processing a trace event has been found."""
        pass

    def end(self):
        """Called at the end of the trace."""
        pass

def process(events, log, analyzer):
    """Invoke an analyzer on each event in a log."""
    if isinstance(events, str):
        events = parse_events(open(events, 'r'))
    if isinstance(log, str):
        log = open(log, 'rb')

    def build_fn(analyzer, event):
        fn = getattr(analyzer, event[0], None)
        if fn is None:
            return analyzer.catchall

        event_argcount = len(event) - 1
        fn_argcount = len(inspect.getargspec(fn)[0]) - 1
        if fn_argcount == event_argcount + 1:
            # Include timestamp as first argument
            return lambda _, rec: fn(*rec[1:2 + event_argcount])
        else:
            # Just arguments, no timestamp
            return lambda _, rec: fn(*rec[2:2 + event_argcount])

    analyzer.begin()
    fn_cache = {}
    for rec in read_trace_file(log):
        event_num = rec[0]
        event = events[event_num]
        if event_num not in fn_cache:
            fn_cache[event_num] = build_fn(analyzer, event)
        fn_cache[event_num](event, rec)
    analyzer.end()

def run(analyzer):
    """Execute an analyzer on a trace file given on the command-line.

    This function is useful as a driver for simple analysis scripts.  More
    advanced scripts will want to call process() instead."""
    import sys

    if len(sys.argv) != 3:
        sys.stderr.write('usage: %s <trace-events> <trace-file>\n' % sys.argv[0])
        sys.exit(1)

    events = parse_events(open(sys.argv[1], 'r'))
    process(events, sys.argv[2], analyzer)

if __name__ == '__main__':
    class Formatter(Analyzer):
        def __init__(self):
            self.last_timestamp = None

        def catchall(self, event, rec):
            timestamp = rec[1]
            if self.last_timestamp is None:
                self.last_timestamp = timestamp
            delta_ns = timestamp - self.last_timestamp
            self.last_timestamp = timestamp

            fields = [event[0], '%0.3f' % (delta_ns / 1000.0)]
            for i in xrange(1, len(event)):
                fields.append('%s=0x%x' % (event[i], rec[i + 1]))
            print ' '.join(fields)

    run(Formatter())
