#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Machinery for generating tracing-related intermediate files.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


import re
import sys

import tracetool.format
import tracetool.backend


def error_write(*lines):
    """Write a set of error lines."""
    sys.stderr.writelines("\n".join(lines) + "\n")

def error(*lines):
    """Write a set of error lines and exit."""
    error_write(*lines)
    sys.exit(1)


def out(*lines, **kwargs):
    """Write a set of output lines.

    You can use kwargs as a shorthand for mapping variables when formating all
    the strings in lines.
    """
    lines = [ l % kwargs for l in lines ]
    sys.stdout.writelines("\n".join(lines) + "\n")


class Arguments:
    """Event arguments description."""

    def __init__(self, args):
        """
        Parameters
        ----------
        args :
            List of (type, name) tuples.
        """
        self._args = args

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
            if arg == 'void':
                continue

            if '*' in arg:
                arg_type, identifier = arg.rsplit('*', 1)
                arg_type += '*'
                identifier = identifier.strip()
            else:
                arg_type, identifier = arg.rsplit(None, 1)

            res.append((arg_type, identifier))
        return Arguments(res)

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
    """

    _CRE = re.compile("((?P<props>.*)\s+)?(?P<name>[^(\s]+)\((?P<args>[^)]*)\)\s*(?P<fmt>\".*)?")

    _VALID_PROPS = set(["disable"])

    def __init__(self, name, props, fmt, args):
        """
        Parameters
        ----------
        name : string
            Event name.
        props : list of str
            Property names.
        fmt : str
            Event printing format.
        args : Arguments
            Event arguments.
        """
        self.name = name
        self.properties = props
        self.fmt = fmt
        self.args = args

        unknown_props = set(self.properties) - self._VALID_PROPS
        if len(unknown_props) > 0:
            raise ValueError("Unknown properties: %s"
                             % ", ".join(unknown_props))

    def copy(self):
        """Create a new copy."""
        return Event(self.name, list(self.properties), self.fmt,
                     self.args.copy(), self)

    @staticmethod
    def build(line_str):
        """Build an Event instance from a string.

        Parameters
        ----------
        line_str : str
            Line describing the event.
        """
        m = Event._CRE.match(line_str)
        assert m is not None
        groups = m.groupdict('')

        name = groups["name"]
        props = groups["props"].split()
        fmt = groups["fmt"]
        args = Arguments.build(groups["args"])

        return Event(name, props, fmt, args)

    def __repr__(self):
        """Evaluable string representation for this object."""
        return "Event('%s %s(%s) %s')" % (" ".join(self.properties),
                                          self.name,
                                          self.args,
                                          self.fmt)

    QEMU_TRACE               = "trace_%(name)s"

    def api(self, fmt=None):
        if fmt is None:
            fmt = Event.QEMU_TRACE
        return fmt % {"name": self.name}


def _read_events(fobj):
    res = []
    for line in fobj:
        if not line.strip():
            continue
        if line.lstrip().startswith('#'):
            continue
        res.append(Event.build(line))
    return res


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


def generate(fevents, format, backend,
             binary=None, probe_prefix=None):
    """Generate the output for the given (format, backend) pair.

    Parameters
    ----------
    fevents : file
        Event description file.
    format : str
        Output format name.
    backend : str
        Output backend name.
    binary : str or None
        See tracetool.backend.dtrace.BINARY.
    probe_prefix : str or None
        See tracetool.backend.dtrace.PROBEPREFIX.
    """
    # fix strange python error (UnboundLocalError tracetool)
    import tracetool

    format = str(format)
    if len(format) is 0:
        raise TracetoolError("format not set")
    if not tracetool.format.exists(format):
        raise TracetoolError("unknown format: %s" % format)
    format = format.replace("-", "_")

    backend = str(backend)
    if len(backend) is 0:
        raise TracetoolError("backend not set")
    if not tracetool.backend.exists(backend):
        raise TracetoolError("unknown backend: %s" % backend)
    backend = backend.replace("-", "_")

    if not tracetool.backend.compatible(backend, format):
        raise TracetoolError("backend '%s' not compatible with format '%s'" %
                             (backend, format))

    import tracetool.backend.dtrace
    tracetool.backend.dtrace.BINARY = binary
    tracetool.backend.dtrace.PROBEPREFIX = probe_prefix

    events = _read_events(fevents)

    if backend == "nop":
        ( e.properies.add("disable") for e in events )

    tracetool.format.generate_begin(format, events)
    tracetool.backend.generate("nop", format,
                               [ e
                                 for e in events
                                 if "disable" in e.properties ])
    tracetool.backend.generate(backend, format,
                               [ e
                                 for e in events
                                 if "disable" not in e.properties ])
    tracetool.format.generate_end(format, events)
