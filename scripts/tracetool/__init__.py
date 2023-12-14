# -*- coding: utf-8 -*-

"""
Machinery for generating tracing-related intermediate files.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


import re
import sys
import weakref

import tracetool.format
import tracetool.backend


def error_write(*lines):
    """Write a set of error lines."""
    sys.stderr.writelines("\n".join(lines) + "\n")

def error(*lines):
    """Write a set of error lines and exit."""
    error_write(*lines)
    sys.exit(1)


out_lineno = 1
out_filename = '<none>'
out_fobj = sys.stdout

def out_open(filename):
    global out_filename, out_fobj
    out_filename = filename
    out_fobj = open(filename, 'wt')

def out(*lines, **kwargs):
    """Write a set of output lines.

    You can use kwargs as a shorthand for mapping variables when formatting all
    the strings in lines.

    The 'out_lineno' kwarg is automatically added to reflect the current output
    file line number. The 'out_next_lineno' kwarg is also automatically added
    with the next output line number. The 'out_filename' kwarg is automatically
    added with the output filename.
    """
    global out_lineno
    output = []
    for l in lines:
        kwargs['out_lineno'] = out_lineno
        kwargs['out_next_lineno'] = out_lineno + 1
        kwargs['out_filename'] = out_filename
        output.append(l % kwargs)
        out_lineno += 1

    out_fobj.writelines("\n".join(output) + "\n")

# We only want to allow standard C types or fixed sized
# integer types. We don't want QEMU specific types
# as we can't assume trace backends can resolve all the
# typedefs
ALLOWED_TYPES = [
    "int",
    "long",
    "short",
    "char",
    "bool",
    "unsigned",
    "signed",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    "uint32_t",
    "int64_t",
    "uint64_t",
    "void",
    "size_t",
    "ssize_t",
    "uintptr_t",
    "ptrdiff_t",
]

def validate_type(name):
    bits = name.split(" ")
    for bit in bits:
        bit = re.sub(r"\*", "", bit)
        if bit == "":
            continue
        if bit == "const":
            continue
        if bit not in ALLOWED_TYPES:
            raise ValueError("Argument type '%s' is not allowed. "
                             "Only standard C types and fixed size integer "
                             "types should be used. struct, union, and "
                             "other complex pointer types should be "
                             "declared as 'void *'" % name)

class Arguments:
    """Event arguments description."""

    def __init__(self, args):
        """
        Parameters
        ----------
        args :
            List of (type, name) tuples or Arguments objects.
        """
        self._args = []
        for arg in args:
            if isinstance(arg, Arguments):
                self._args.extend(arg._args)
            else:
                self._args.append(arg)

    def copy(self):
        """Create a new copy."""
        return Arguments(list(self._args))

    @staticmethod
    def build(arg_str):
        """Build and Arguments instance from an argument string.

        Parameters
        ----------
        arg_str : str
            String describing the event arguments.
        """
        res = []
        for arg in arg_str.split(","):
            arg = arg.strip()
            if not arg:
                raise ValueError("Empty argument (did you forget to use 'void'?)")
            if arg == 'void':
                continue

            if '*' in arg:
                arg_type, identifier = arg.rsplit('*', 1)
                arg_type += '*'
                identifier = identifier.strip()
            else:
                arg_type, identifier = arg.rsplit(None, 1)

            validate_type(arg_type)
            res.append((arg_type, identifier))
        return Arguments(res)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return Arguments(self._args[index])
        else:
            return self._args[index]

    def __iter__(self):
        """Iterate over the (type, name) pairs."""
        return iter(self._args)

    def __len__(self):
        """Number of arguments."""
        return len(self._args)

    def __str__(self):
        """String suitable for declaring function arguments."""
        if len(self._args) == 0:
            return "void"
        else:
            return ", ".join([ " ".join([t, n]) for t,n in self._args ])

    def __repr__(self):
        """Evaluable string representation for this object."""
        return "Arguments(\"%s\")" % str(self)

    def names(self):
        """List of argument names."""
        return [ name for _, name in self._args ]

    def types(self):
        """List of argument types."""
        return [ type_ for type_, _ in self._args ]

    def casted(self):
        """List of argument names casted to their type."""
        return ["(%s)%s" % (type_, name) for type_, name in self._args]


class Event(object):
    """Event description.

    Attributes
    ----------
    name : str
        The event name.
    fmt : str
        The event format string.
    properties : set(str)
        Properties of the event.
    args : Arguments
        The event arguments.
    lineno : int
        The line number in the input file.
    filename : str
        The path to the input file.

    """

    _CRE = re.compile(r"((?P<props>[\w\s]+)\s+)?"
                      r"(?P<name>\w+)"
                      r"\((?P<args>[^)]*)\)"
                      r"\s*"
                      r"(?:(?:(?P<fmt_trans>\".+),)?\s*(?P<fmt>\".+))?"
                      r"\s*")

    _VALID_PROPS = set(["disable", "vcpu"])

    def __init__(self, name, props, fmt, args, lineno, filename, orig=None,
                 event_trans=None, event_exec=None):
        """
        Parameters
        ----------
        name : string
            Event name.
        props : list of str
            Property names.
        fmt : str, list of str
            Event printing format string(s).
        args : Arguments
            Event arguments.
        lineno : int
            The line number in the input file.
        filename : str
            The path to the input file.
        orig : Event or None
            Original Event before transformation/generation.
        event_trans : Event or None
            Generated translation-time event ("tcg" property).
        event_exec : Event or None
            Generated execution-time event ("tcg" property).

        """
        self.name = name
        self.properties = props
        self.fmt = fmt
        self.args = args
        self.lineno = int(lineno)
        self.filename = str(filename)
        self.event_trans = event_trans
        self.event_exec = event_exec

        if len(args) > 10:
            raise ValueError("Event '%s' has more than maximum permitted "
                             "argument count" % name)

        if orig is None:
            self.original = weakref.ref(self)
        else:
            self.original = orig

        unknown_props = set(self.properties) - self._VALID_PROPS
        if len(unknown_props) > 0:
            raise ValueError("Unknown properties: %s"
                             % ", ".join(unknown_props))
        assert isinstance(self.fmt, str) or len(self.fmt) == 2

    def copy(self):
        """Create a new copy."""
        return Event(self.name, list(self.properties), self.fmt,
                     self.args.copy(), self.lineno, self.filename,
                     self, self.event_trans, self.event_exec)

    @staticmethod
    def build(line_str, lineno, filename):
        """Build an Event instance from a string.

        Parameters
        ----------
        line_str : str
            Line describing the event.
        lineno : int
            Line number in input file.
        filename : str
            Path to input file.
        """
        m = Event._CRE.match(line_str)
        assert m is not None
        groups = m.groupdict('')

        name = groups["name"]
        props = groups["props"].split()
        fmt = groups["fmt"]
        fmt_trans = groups["fmt_trans"]
        if fmt.find("%m") != -1 or fmt_trans.find("%m") != -1:
            raise ValueError("Event format '%m' is forbidden, pass the error "
                             "as an explicit trace argument")
        if fmt.endswith(r'\n"'):
            raise ValueError("Event format must not end with a newline "
                             "character")

        if len(fmt_trans) > 0:
            fmt = [fmt_trans, fmt]
        args = Arguments.build(groups["args"])

        event = Event(name, props, fmt, args, lineno, filename)

        # add implicit arguments when using the 'vcpu' property
        import tracetool.vcpu
        event = tracetool.vcpu.transform_event(event)

        return event

    def __repr__(self):
        """Evaluable string representation for this object."""
        if isinstance(self.fmt, str):
            fmt = self.fmt
        else:
            fmt = "%s, %s" % (self.fmt[0], self.fmt[1])
        return "Event('%s %s(%s) %s')" % (" ".join(self.properties),
                                          self.name,
                                          self.args,
                                          fmt)
    # Star matching on PRI is dangerous as one might have multiple
    # arguments with that format, hence the non-greedy version of it.
    _FMT = re.compile(r"(%[\d\.]*\w+|%.*?PRI\S+)")

    def formats(self):
        """List conversion specifiers in the argument print format string."""
        assert not isinstance(self.fmt, list)
        return self._FMT.findall(self.fmt)

    QEMU_TRACE               = "trace_%(name)s"
    QEMU_TRACE_NOCHECK       = "_nocheck__" + QEMU_TRACE
    QEMU_TRACE_TCG           = QEMU_TRACE + "_tcg"
    QEMU_DSTATE              = "_TRACE_%(NAME)s_DSTATE"
    QEMU_BACKEND_DSTATE      = "TRACE_%(NAME)s_BACKEND_DSTATE"
    QEMU_EVENT               = "_TRACE_%(NAME)s_EVENT"

    def api(self, fmt=None):
        if fmt is None:
            fmt = Event.QEMU_TRACE
        return fmt % {"name": self.name, "NAME": self.name.upper()}


def read_events(fobj, fname):
    """Generate the output for the given (format, backends) pair.

    Parameters
    ----------
    fobj : file
        Event description file.
    fname : str
        Name of event file

    Returns a list of Event objects
    """

    events = []
    for lineno, line in enumerate(fobj, 1):
        if line[-1] != '\n':
            raise ValueError("%s does not end with a new line" % fname)
        if not line.strip():
            continue
        if line.lstrip().startswith('#'):
            continue

        try:
            event = Event.build(line, lineno, fname)
        except ValueError as e:
            arg0 = 'Error at %s:%d: %s' % (fname, lineno, e.args[0])
            e.args = (arg0,) + e.args[1:]
            raise

        events.append(event)

    return events


class TracetoolError (Exception):
    """Exception for calls to generate."""
    pass


def try_import(mod_name, attr_name=None, attr_default=None):
    """Try to import a module and get an attribute from it.

    Parameters
    ----------
    mod_name : str
        Module name.
    attr_name : str, optional
        Name of an attribute in the module.
    attr_default : optional
        Default value if the attribute does not exist in the module.

    Returns
    -------
    A pair indicating whether the module could be imported and the module or
    object or attribute value.
    """
    try:
        module = __import__(mod_name, globals(), locals(), ["__package__"])
        if attr_name is None:
            return True, module
        return True, getattr(module, str(attr_name), attr_default)
    except ImportError:
        return False, None


def generate(events, group, format, backends,
             binary=None, probe_prefix=None):
    """Generate the output for the given (format, backends) pair.

    Parameters
    ----------
    events : list
        list of Event objects to generate for
    group: str
        Name of the tracing group
    format : str
        Output format name.
    backends : list
        Output backend names.
    binary : str or None
        See tracetool.backend.dtrace.BINARY.
    probe_prefix : str or None
        See tracetool.backend.dtrace.PROBEPREFIX.
    """
    # fix strange python error (UnboundLocalError tracetool)
    import tracetool

    format = str(format)
    if len(format) == 0:
        raise TracetoolError("format not set")
    if not tracetool.format.exists(format):
        raise TracetoolError("unknown format: %s" % format)

    if len(backends) == 0:
        raise TracetoolError("no backends specified")
    for backend in backends:
        if not tracetool.backend.exists(backend):
            raise TracetoolError("unknown backend: %s" % backend)
    backend = tracetool.backend.Wrapper(backends, format)

    import tracetool.backend.dtrace
    tracetool.backend.dtrace.BINARY = binary
    tracetool.backend.dtrace.PROBEPREFIX = probe_prefix

    tracetool.format.generate(events, format, backend, group)
