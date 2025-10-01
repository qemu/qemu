# SPDX-License-Identifier: GPL-2.0-or-later

"""
Machinery for generating tracing-related intermediate files.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


import os
import re
import sys
from pathlib import PurePath

import tracetool.backend
import tracetool.format


def error_write(*lines):
    """Write a set of error lines."""
    sys.stderr.writelines("\n".join(lines) + "\n")

def error(*lines):
    """Write a set of error lines and exit."""
    error_write(*lines)
    sys.exit(1)

FMT_TOKEN = re.compile(r'''(?:
                       " ( (?: [^"\\] | \\[\\"abfnrt] |            # a string literal
                               \\x[0-9a-fA-F][0-9a-fA-F]) *? ) "
                       | ( PRI [duixX] (?:8|16|32|64|PTR|MAX) )    # a PRIxxx macro
                       | \s+                                       # spaces (ignored)
                       )''', re.X)

PRI_SIZE_MAP = {
    '8':  'hh',
    '16': 'h',
    '32': '',
    '64': 'll',
    'PTR': 't',
    'MAX': 'j',
}

def expand_format_string(c_fmt, prefix=""):
    def pri_macro_to_fmt(pri_macro):
        assert pri_macro.startswith("PRI")
        fmt_type = pri_macro[3]  # 'd', 'i', 'u', or 'x'
        fmt_size = pri_macro[4:]  # '8', '16', '32', '64', 'PTR', 'MAX'

        size = PRI_SIZE_MAP.get(fmt_size, None)
        if size is None:
            raise Exception(f"unknown macro {pri_macro}")
        return size + fmt_type

    result = prefix
    pos = 0
    while pos < len(c_fmt):
        m = FMT_TOKEN.match(c_fmt, pos)
        if not m:
            print("No match at position", pos, ":", repr(c_fmt[pos:]), file=sys.stderr)
            raise Exception("syntax error in trace file")
        if m[1]:
            substr = m[1]
        elif m[2]:
            substr = pri_macro_to_fmt(m[2])
        else:
            substr = ""
        result += substr
        pos = m.end()
    return result

out_lineno = 1
out_filename = '<none>'
out_fobj = sys.stdout

def out_open(filename):
    global out_filename, out_fobj
    out_filename = posix_relpath(filename)
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

C_TYPE_KEYWORDS = {"char", "int", "void", "short", "long", "signed", "unsigned"}

C_TO_RUST_TYPE_MAP = {
    "int": "std::ffi::c_int",
    "long": "std::ffi::c_long",
    "long long": "std::ffi::c_longlong",
    "short": "std::ffi::c_short",
    "char": "std::ffi::c_char",
    "bool": "bool",
    "unsigned": "std::ffi::c_uint",
    # multiple keywords, keep them sorted
    "long unsigned": "std::ffi::c_long",
    "long long unsigned": "std::ffi::c_ulonglong",
    "short unsigned": "std::ffi::c_ushort",
    "char unsigned": "u8",
    "int8_t": "i8",
    "uint8_t": "u8",
    "int16_t": "i16",
    "uint16_t": "u16",
    "int32_t": "i32",
    "uint32_t": "u32",
    "int64_t": "i64",
    "uint64_t": "u64",
    "void": "()",
    "size_t": "usize",
    "ssize_t": "isize",
    "uintptr_t": "usize",
    "ptrdiff_t": "isize",
}

# Rust requires manual casting of <32-bit types when passing them to
# variable-argument functions.
RUST_VARARGS_SMALL_TYPES = {
    "std::ffi::c_short",
    "std::ffi::c_ushort",
    "std::ffi::c_char",
    "i8",
    "u8",
    "i16",
    "u16",
    "bool",
}

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

def c_type_to_rust(name):
    ptr = False
    const = False
    name = name.rstrip()
    if name[-1] == '*':
        name = name[:-1].rstrip()
        ptr = True
        if name[-1] == '*':
            # pointers to pointers are the same as void*
            name = "void"

    bits = name.split()
    if "const" in bits:
        const = True
        bits.remove("const")
    if bits[0] in C_TYPE_KEYWORDS:
        if "signed" in bits:
            bits.remove("signed")
        if len(bits) > 1 and "int" in bits:
            bits.remove("int")
        bits.sort()
        name = ' '.join(bits)
    else:
        if len(bits) > 1:
            raise ValueError("Invalid type '%s'." % name)
        name = bits[0]

    ty = C_TO_RUST_TYPE_MAP[name.strip()]
    if ptr:
        ty = f'*{"const" if const else "mut"} {ty}'
    return ty

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
        def onearg(t, n):
            if t[-1] == '*':
                return "".join([t, n])
            else:
                return " ".join([t, n])

        if len(self._args) == 0:
            return "void"
        else:
            return ", ".join([ onearg(t, n) for t,n in self._args ])

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

    def rust_decl_extern(self):
        """Return a Rust argument list for an extern "C" function"""
        return ", ".join((f"_{name}: {c_type_to_rust(type_)}"
                          for type_, name in self._args))

    def rust_decl(self):
        """Return a Rust argument list for a tracepoint function"""
        def decl_type(type_):
            if type_ == "const char *":
                return "&std::ffi::CStr"
            return c_type_to_rust(type_)

        return ", ".join((f"_{name}: {decl_type(type_)}"
                          for type_, name in self._args))

    def rust_call_extern(self):
        """Return a Rust argument list for a call to an extern "C" function"""
        def rust_cast(name, type_):
            if type_ == "const char *":
                return f"_{name}.as_ptr()"
            return f"_{name}"

        return ", ".join((rust_cast(name, type_) for type_, name in self._args))

    def rust_call_varargs(self):
        """Return a Rust argument list for a call to a C varargs function"""
        def rust_cast(name, type_):
            if type_ == "const char *":
                return f"_{name}.as_ptr()"

            type_ = c_type_to_rust(type_)
            if type_ in RUST_VARARGS_SMALL_TYPES:
                return f"_{name} as std::ffi::c_int"
            return f"_{name} /* as {type_} */"

        return ", ".join((rust_cast(name, type_) for type_, name in self._args))


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
                      r"(?P<fmt>\".+)?"
                      r"\s*")

    _VALID_PROPS = set(["disable"])

    def __init__(self, name, props, fmt, args, lineno, filename):
        """
        Parameters
        ----------
        name : string
            Event name.
        props : list of str
            Property names.
        fmt : str
            Event printing format string.
        args : Arguments
            Event arguments.
        lineno : int
            The line number in the input file.
        filename : str
            The path to the input file.

        """
        self.name = name
        self.properties = props
        self.fmt = fmt
        self.args = args
        self.lineno = int(lineno)
        self.filename = str(filename)

        if len(args) > 10:
            raise ValueError("Event '%s' has more than maximum permitted "
                             "argument count" % name)

        unknown_props = set(self.properties) - self._VALID_PROPS
        if len(unknown_props) > 0:
            raise ValueError("Unknown properties: %s"
                             % ", ".join(unknown_props))


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
        if fmt.find("%m") != -1:
            raise ValueError("Event format '%m' is forbidden, pass the error "
                             "as an explicit trace argument")
        if fmt.endswith(r'\n"'):
            raise ValueError("Event format must not end with a newline "
                             "character")
        if '\\n' in fmt:
            raise ValueError("Event format must not use new line character")

        args = Arguments.build(groups["args"])

        return Event(name, props, fmt, args, lineno, posix_relpath(filename))

    def __repr__(self):
        """Evaluable string representation for this object."""
        return "Event('%s %s(%s) %s')" % (" ".join(self.properties),
                                          self.name,
                                          self.args,
                                          self.fmt)
    # Star matching on PRI is dangerous as one might have multiple
    # arguments with that format, hence the non-greedy version of it.
    _FMT = re.compile(r"(%[\d\.]*\w+|%.*?PRI\S+)")

    def formats(self):
        """List conversion specifiers in the argument print format string."""
        return self._FMT.findall(self.fmt)

    QEMU_TRACE               = "trace_%(name)s"
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

def posix_relpath(path, start=None):
    try:
        path = os.path.relpath(path, start)
    except ValueError:
        pass
    return PurePath(path).as_posix()
