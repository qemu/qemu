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

import sys
import struct
import inspect
import warnings
from tracetool import read_events, Event
from tracetool.backend.simple import is_string

__all__ = ['Analyzer', 'Analyzer2', 'process', 'run']

# This is the binary format that the QEMU "simple" trace backend
# emits. There is no specification documentation because the format is
# not guaranteed to be stable. Trace files must be parsed with the
# same trace-events-all file and the same simpletrace.py file that
# QEMU was built with.
header_event_id = 0xffffffffffffffff
header_magic    = 0xf2b177cb0aa429b4
dropped_event_id = 0xfffffffffffffffe

record_type_mapping = 0
record_type_event = 1

log_header_fmt = '=QQQ'
rec_header_fmt = '=QQII'
rec_header_fmt_len = struct.calcsize(rec_header_fmt)

class SimpleException(Exception):
    pass

def read_header(fobj, hfmt):
    '''Read a trace record header'''
    hlen = struct.calcsize(hfmt)
    hdr = fobj.read(hlen)
    if len(hdr) != hlen:
        raise SimpleException('Error reading header. Wrong filetype provided?')
    return struct.unpack(hfmt, hdr)

def get_mapping(fobj):
    (event_id, ) = struct.unpack('=Q', fobj.read(8))
    (len, ) = struct.unpack('=L', fobj.read(4))
    name = fobj.read(len).decode()

    return (event_id, name)

def read_record(fobj):
    """Deserialize a trace record from a file into a tuple (event_num, timestamp, pid, args)."""
    event_id, timestamp_ns, record_length, record_pid = read_header(fobj, rec_header_fmt)
    args_payload = fobj.read(record_length - rec_header_fmt_len)
    return (event_id, timestamp_ns, record_pid, args_payload)

def read_trace_header(fobj):
    """Read and verify trace file header"""
    _header_event_id, _header_magic, log_version = read_header(fobj, log_header_fmt)
    if _header_event_id != header_event_id:
        raise ValueError(f'Not a valid trace file, header id {_header_event_id} != {header_event_id}')
    if _header_magic != header_magic:
        raise ValueError(f'Not a valid trace file, header magic {_header_magic} != {header_magic}')

    if log_version not in [0, 2, 3, 4]:
        raise ValueError(f'Unknown version {log_version} of tracelog format!')
    if log_version != 4:
        raise ValueError(f'Log format {log_version} not supported with this QEMU release!')

def read_trace_records(events, fobj, read_header):
    """Deserialize trace records from a file, yielding record tuples (event, event_num, timestamp, pid, arg1, ..., arg6).

    Args:
        event_mapping (str -> Event): events dict, indexed by name
        fobj (file): input file
        read_header (bool): whether headers were read from fobj

    """
    frameinfo = inspect.getframeinfo(inspect.currentframe())
    dropped_event = Event.build("Dropped_Event(uint64_t num_events_dropped)",
                                frameinfo.lineno + 1, frameinfo.filename)

    event_mapping = {e.name: e for e in events}
    event_mapping["dropped"] = dropped_event
    event_id_to_name = {dropped_event_id: "dropped"}

    # If there is no header assume event ID mapping matches events list
    if not read_header:
        for event_id, event in enumerate(events):
            event_id_to_name[event_id] = event.name

    while True:
        t = fobj.read(8)
        if len(t) == 0:
            break

        (rectype, ) = struct.unpack('=Q', t)
        if rectype == record_type_mapping:
            event_id, event_name = get_mapping(fobj)
            event_id_to_name[event_id] = event_name
        else:
            event_id, timestamp_ns, pid, args_payload = read_record(fobj)
            event_name = event_id_to_name[event_id]

            try:
                event = event_mapping[event_name]
            except KeyError as e:
                raise SimpleException(
                    f'{e} event is logged but is not declared in the trace events'
                    'file, try using trace-events-all instead.'
                )

            offset = 0
            args = []
            for type, _ in event.args:
                if is_string(type):
                    (length,) = struct.unpack_from('=L', args_payload, offset=offset)
                    offset += 4
                    s = args_payload[offset:offset+length]
                    offset += length
                    args.append(s)
                else:
                    (value,) = struct.unpack_from('=Q', args_payload, offset=offset)
                    offset += 8
                    args.append(value)

            yield (event_mapping[event_name], event_name, timestamp_ns, pid) + tuple(args)

class Analyzer:
    """[Deprecated. Refer to Analyzer2 instead.]

    A trace file analyzer which processes trace records.

    An analyzer can be passed to run() or process().  The begin() method is
    invoked, then each trace record is processed, and finally the end() method
    is invoked. When Analyzer is used as a context-manager (using the `with`
    statement), begin() and end() are called automatically.

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

    def _build_fn(self, event):
        fn = getattr(self, event.name, None)
        if fn is None:
            # Return early to avoid costly call to inspect.getfullargspec
            return self.catchall

        event_argcount = len(event.args)
        fn_argcount = len(inspect.getfullargspec(fn)[0]) - 1
        if fn_argcount == event_argcount + 1:
            # Include timestamp as first argument
            return lambda _, rec: fn(*(rec[1:2] + rec[3:3 + event_argcount]))
        elif fn_argcount == event_argcount + 2:
            # Include timestamp and pid
            return lambda _, rec: fn(*rec[1:3 + event_argcount])
        else:
            # Just arguments, no timestamp or pid
            return lambda _, rec: fn(*rec[3:3 + event_argcount])

    def _process_event(self, rec_args, *, event, event_id, timestamp_ns, pid, **kwargs):
        warnings.warn(
            "Use of deprecated Analyzer class. Refer to Analyzer2 instead.",
            DeprecationWarning,
        )

        if not hasattr(self, '_fn_cache'):
            # NOTE: Cannot depend on downstream subclasses to have
            # super().__init__() because of legacy.
            self._fn_cache = {}

        rec = (event_id, timestamp_ns, pid, *rec_args)
        if event_id not in self._fn_cache:
            self._fn_cache[event_id] = self._build_fn(event)
        self._fn_cache[event_id](event, rec)

    def end(self):
        """Called at the end of the trace."""
        pass

    def __enter__(self):
        self.begin()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            self.end()
        return False

class Analyzer2(Analyzer):
    """A trace file analyzer which processes trace records.

    An analyzer can be passed to run() or process().  The begin() method is
    invoked, then each trace record is processed, and finally the end() method
    is invoked. When Analyzer is used as a context-manager (using the `with`
    statement), begin() and end() are called automatically.

    If a method matching a trace event name exists, it is invoked to process
    that trace record.  Otherwise the catchall() method is invoked.

    The methods are called with a set of keyword-arguments. These can be ignored
    using `**kwargs` or defined like any keyword-argument.

    The following keyword-arguments are available, but make sure to have an
    **kwargs to allow for unmatched arguments in the future:
        event: Event object of current trace
        event_id: The id of the event in the current trace file
        timestamp_ns: The timestamp in nanoseconds of the trace
        pid: The process id recorded for the given trace

    Example:
    The following method handles the runstate_set(int new_state) trace event::

      def runstate_set(self, new_state, **kwargs):
          ...

    The method can also explicitly take a timestamp keyword-argument with the
    trace event arguments::

      def runstate_set(self, new_state, *, timestamp_ns, **kwargs):
          ...

    Timestamps have the uint64_t type and are in nanoseconds.

    The pid can be included in addition to the timestamp and is useful when
    dealing with traces from multiple processes:

      def runstate_set(self, new_state, *, timestamp_ns, pid, **kwargs):
          ...
    """

    def catchall(self, *rec_args, event, timestamp_ns, pid, event_id, **kwargs):
        """Called if no specific method for processing a trace event has been found."""
        pass

    def _process_event(self, rec_args, *, event, **kwargs):
        fn = getattr(self, event.name, self.catchall)
        fn(*rec_args, event=event, **kwargs)

def process(events, log, analyzer, read_header=True):
    """Invoke an analyzer on each event in a log.
    Args:
        events (file-object or list or str): events list or file-like object or file path as str to read event data from
        log (file-object or str): file-like object or file path as str to read log data from
        analyzer (Analyzer): Instance of Analyzer to interpret the event data
        read_header (bool, optional): Whether to read header data from the log data. Defaults to True.
    """

    if isinstance(events, str):
        with open(events, 'r') as f:
            events_list = read_events(f, events)
    elif isinstance(events, list):
        # Treat as a list of events already produced by tracetool.read_events
        events_list = events
    else:
        # Treat as an already opened file-object
        events_list = read_events(events, events.name)

    if isinstance(log, str):
        with open(log, 'rb') as log_fobj:
            _process(events_list, log_fobj, analyzer, read_header)
    else:
        # Treat `log` as an already opened file-object. We will not close it,
        # as we do not own it.
        _process(events_list, log, analyzer, read_header)

def _process(events, log_fobj, analyzer, read_header=True):
    """Internal function for processing

    Args:
        events (list): list of events already produced by tracetool.read_events
        log_fobj (file): file-object to read log data from
        analyzer (Analyzer): the Analyzer to interpret the event data
        read_header (bool, optional): Whether to read header data from the log data. Defaults to True.
    """

    if read_header:
        read_trace_header(log_fobj)

    with analyzer:
        for event, event_id, timestamp_ns, record_pid, *rec_args in read_trace_records(events, log_fobj, read_header):
            analyzer._process_event(
                rec_args,
                event=event,
                event_id=event_id,
                timestamp_ns=timestamp_ns,
                pid=record_pid,
            )

def run(analyzer):
    """Execute an analyzer on a trace file given on the command-line.

    This function is useful as a driver for simple analysis scripts.  More
    advanced scripts will want to call process() instead."""

    try:
        # NOTE: See built-in `argparse` module for a more robust cli interface
        *no_header, trace_event_path, trace_file_path = sys.argv[1:]
        assert no_header == [] or no_header == ['--no-header'], 'Invalid no-header argument'
    except (AssertionError, ValueError):
        raise SimpleException(f'usage: {sys.argv[0]} [--no-header] <trace-events> <trace-file>\n')

    with open(trace_event_path, 'r') as events_fobj, open(trace_file_path, 'rb') as log_fobj:
        process(events_fobj, log_fobj, analyzer, read_header=not no_header)

if __name__ == '__main__':
    class Formatter2(Analyzer2):
        def __init__(self):
            self.last_timestamp_ns = None

        def catchall(self, *rec_args, event, timestamp_ns, pid, event_id):
            if self.last_timestamp_ns is None:
                self.last_timestamp_ns = timestamp_ns
            delta_ns = timestamp_ns - self.last_timestamp_ns
            self.last_timestamp_ns = timestamp_ns

            fields = [
                f'{name}={r}' if is_string(type) else f'{name}=0x{r:x}'
                for r, (type, name) in zip(rec_args, event.args)
            ]
            print(f'{event.name} {delta_ns / 1000:0.3f} {pid=} ' + ' '.join(fields))

    try:
        run(Formatter2())
    except SimpleException as e:
        sys.stderr.write(str(e) + "\n")
        sys.exit(1)
