#!/usr/bin/env python3
#
# Pretty-printer for simple trace backend binary trace files
#
# Copyright IBM, Corp. 2010
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# For help see docs/devel/tracing.rst

import struct
import inspect
from tracetool import read_events, Event
from tracetool.backend.simple import is_string

header_event_id = 0xffffffffffffffff
header_magic    = 0xf2b177cb0aa429b4
dropped_event_id = 0xfffffffffffffffe

record_type_mapping = 0
record_type_event = 1

log_header_fmt = '=QQQ'
rec_header_fmt = '=QQII'

def read_header(fobj, hfmt):
    '''Read a trace record header'''
    hlen = struct.calcsize(hfmt)
    hdr = fobj.read(hlen)
    if len(hdr) != hlen:
        return None
    return struct.unpack(hfmt, hdr)

def get_record(edict, idtoname, rechdr, fobj):
    """Deserialize a trace record from a file into a tuple
       (name, timestamp, pid, arg1, ..., arg6)."""
    if rechdr is None:
        return None
    if rechdr[0] != dropped_event_id:
        event_id = rechdr[0]
        name = idtoname[event_id]
        rec = (name, rechdr[1], rechdr[3])
        try:
            event = edict[name]
        except KeyError as e:
            import sys
            sys.stderr.write('%s event is logged but is not declared ' \
                             'in the trace events file, try using ' \
                             'trace-events-all instead.\n' % str(e))
            sys.exit(1)

        for type, name in event.args:
            if is_string(type):
                l = fobj.read(4)
                (len,) = struct.unpack('=L', l)
                s = fobj.read(len)
                rec = rec + (s,)
            else:
                (value,) = struct.unpack('=Q', fobj.read(8))
                rec = rec + (value,)
    else:
        rec = ("dropped", rechdr[1], rechdr[3])
        (value,) = struct.unpack('=Q', fobj.read(8))
        rec = rec + (value,)
    return rec

def get_mapping(fobj):
    (event_id, ) = struct.unpack('=Q', fobj.read(8))
    (len, ) = struct.unpack('=L', fobj.read(4))
    name = fobj.read(len).decode()

    return (event_id, name)

def read_record(edict, idtoname, fobj):
    """Deserialize a trace record from a file into a tuple (event_num, timestamp, pid, arg1, ..., arg6)."""
    rechdr = read_header(fobj, rec_header_fmt)
    return get_record(edict, idtoname, rechdr, fobj)

def read_trace_header(fobj):
    """Read and verify trace file header"""
    header = read_header(fobj, log_header_fmt)
    if header is None:
        raise ValueError('Not a valid trace file!')
    if header[0] != header_event_id:
        raise ValueError('Not a valid trace file, header id %d != %d' %
                         (header[0], header_event_id))
    if header[1] != header_magic:
        raise ValueError('Not a valid trace file, header magic %d != %d' %
                         (header[1], header_magic))

    log_version = header[2]
    if log_version not in [0, 2, 3, 4]:
        raise ValueError('Unknown version of tracelog format!')
    if log_version != 4:
        raise ValueError('Log format %d not supported with this QEMU release!'
                         % log_version)

def read_trace_records(edict, idtoname, fobj):
    """Deserialize trace records from a file, yielding record tuples (event_num, timestamp, pid, arg1, ..., arg6).

    Note that `idtoname` is modified if the file contains mapping records.

    Args:
        edict (str -> Event): events dict, indexed by name
        idtoname (int -> str): event names dict, indexed by event ID
        fobj (file): input file

    """
    while True:
        t = fobj.read(8)
        if len(t) == 0:
            break

        (rectype, ) = struct.unpack('=Q', t)
        if rectype == record_type_mapping:
            event_id, name = get_mapping(fobj)
            idtoname[event_id] = name
        else:
            rec = read_record(edict, idtoname, fobj)

            yield rec

class Analyzer(object):
    """A trace file analyzer which processes trace records.

    An analyzer can be passed to run() or process().  The begin() method is
    invoked, then each trace record is processed, and finally the end() method
    is invoked.

    If a method matching a trace event name exists, it is invoked to process
    that trace record.  Otherwise the catchall() method is invoked.

    Example:
    The following method handles the runstate_set(int new_state) trace event::

      def runstate_set(self, new_state):
          ...

    The method can also take a timestamp argument before the trace event
    arguments::

      def runstate_set(self, timestamp, new_state):
          ...

    Timestamps have the uint64_t type and are in nanoseconds.

    The pid can be included in addition to the timestamp and is useful when
    dealing with traces from multiple processes::

      def runstate_set(self, timestamp, pid, new_state):
          ...
    """

    def begin(self):
        """Called at the start of the trace."""
        pass

    def catchall(self, event, rec):
        """Called if no specific method for processing a trace event has been found."""
        pass

    def end(self):
        """Called at the end of the trace."""
        pass

def process(events, log, analyzer, read_header=True):
    """Invoke an analyzer on each event in a log."""
    if isinstance(events, str):
        events = read_events(open(events, 'r'), events)
    if isinstance(log, str):
        log = open(log, 'rb')

    if read_header:
        read_trace_header(log)

    frameinfo = inspect.getframeinfo(inspect.currentframe())
    dropped_event = Event.build("Dropped_Event(uint64_t num_events_dropped)",
                                frameinfo.lineno + 1, frameinfo.filename)
    edict = {"dropped": dropped_event}
    idtoname = {dropped_event_id: "dropped"}

    for event in events:
        edict[event.name] = event

    # If there is no header assume event ID mapping matches events list
    if not read_header:
        for event_id, event in enumerate(events):
            idtoname[event_id] = event.name

    def build_fn(analyzer, event):
        if isinstance(event, str):
            return analyzer.catchall

        fn = getattr(analyzer, event.name, None)
        if fn is None:
            return analyzer.catchall

        event_argcount = len(event.args)
        fn_argcount = len(inspect.getargspec(fn)[0]) - 1
        if fn_argcount == event_argcount + 1:
            # Include timestamp as first argument
            return lambda _, rec: fn(*(rec[1:2] + rec[3:3 + event_argcount]))
        elif fn_argcount == event_argcount + 2:
            # Include timestamp and pid
            return lambda _, rec: fn(*rec[1:3 + event_argcount])
        else:
            # Just arguments, no timestamp or pid
            return lambda _, rec: fn(*rec[3:3 + event_argcount])

    analyzer.begin()
    fn_cache = {}
    for rec in read_trace_records(edict, idtoname, log):
        event_num = rec[0]
        event = edict[event_num]
        if event_num not in fn_cache:
            fn_cache[event_num] = build_fn(analyzer, event)
        fn_cache[event_num](event, rec)
    analyzer.end()

def run(analyzer):
    """Execute an analyzer on a trace file given on the command-line.

    This function is useful as a driver for simple analysis scripts.  More
    advanced scripts will want to call process() instead."""
    import sys

    read_header = True
    if len(sys.argv) == 4 and sys.argv[1] == '--no-header':
        read_header = False
        del sys.argv[1]
    elif len(sys.argv) != 3:
        sys.stderr.write('usage: %s [--no-header] <trace-events> ' \
                         '<trace-file>\n' % sys.argv[0])
        sys.exit(1)

    events = read_events(open(sys.argv[1], 'r'), sys.argv[1])
    process(events, sys.argv[2], analyzer, read_header=read_header)

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

            fields = [event.name, '%0.3f' % (delta_ns / 1000.0),
                      'pid=%d' % rec[2]]
            i = 3
            for type, name in event.args:
                if is_string(type):
                    fields.append('%s=%s' % (name, rec[i]))
                else:
                    fields.append('%s=0x%x' % (name, rec[i]))
                i += 1
            print(' '.join(fields))

    run(Formatter())
