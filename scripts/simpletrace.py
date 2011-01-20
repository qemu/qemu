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

import sys
import struct
import re

header_event_id = 0xffffffffffffffff
header_magic    = 0xf2b177cb0aa429b4
header_version  = 0

trace_fmt = '=QQQQQQQQ'
trace_len = struct.calcsize(trace_fmt)
event_re  = re.compile(r'(disable\s+)?([a-zA-Z0-9_]+)\(([^)]*)\).*')

def err(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)

def parse_events(fobj):
    """Parse a trace-events file."""

    def get_argnames(args):
        """Extract argument names from a parameter list."""
        return tuple(arg.split()[-1].lstrip('*') for arg in args.split(','))

    events = {}
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
    """Deserialize a trace record from a file."""
    s = fobj.read(trace_len)
    if len(s) != trace_len:
        return None
    return struct.unpack(trace_fmt, s)

def read_trace_file(fobj):
    """Deserialize trace records from a file."""
    header = read_record(fobj)
    if header is None or \
       header[0] != header_event_id or \
       header[1] != header_magic or \
       header[2] != header_version:
        err('not a trace file or incompatible version')

    while True:
        rec = read_record(fobj)
        if rec is None:
            break

        yield rec

class Formatter(object):
    def __init__(self, events):
        self.events = events
        self.last_timestamp = None

    def format_record(self, rec):
        if self.last_timestamp is None:
            self.last_timestamp = rec[1]
        delta_ns = rec[1] - self.last_timestamp
        self.last_timestamp = rec[1]

        event = self.events[rec[0]]
        fields = [event[0], '%0.3f' % (delta_ns / 1000.0)]
        for i in xrange(1, len(event)):
            fields.append('%s=0x%x' % (event[i], rec[i + 1]))
        return ' '.join(fields)

if len(sys.argv) != 3:
    err('usage: %s <trace-events> <trace-file>' % sys.argv[0])

events = parse_events(open(sys.argv[1], 'r'))
formatter = Formatter(events)
for rec in read_trace_file(open(sys.argv[2], 'rb')):
    print formatter.format_record(rec)
