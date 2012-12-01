#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
DTrace/SystemTAP backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PROBEPREFIX = None

def _probeprefix():
    if PROBEPREFIX is None:
        raise ValueError("you must set PROBEPREFIX")
    return PROBEPREFIX


BINARY = None

def _binary():
    if BINARY is None:
        raise ValueError("you must set BINARY")
    return BINARY


def c(events):
    pass


def h(events):
    out('#include "trace-dtrace.h"',
        '')

    for e in events:
        out('static inline void trace_%(name)s(%(args)s) {',
            '    QEMU_%(uppername)s(%(argnames)s);',
            '}',
            name = e.name,
            args = e.args,
            uppername = e.name.upper(),
            argnames = ", ".join(e.args.names()),
            )


def d(events):
    out('provider qemu {')

    for e in events:
        args = str(e.args)

        # DTrace provider syntax expects foo() for empty
        # params, not foo(void)
        if args == 'void':
            args = ''

        # Define prototype for probe arguments
        out('',
            'probe %(name)s(%(args)s);',
            name = e.name,
            args = args,
            )

    out('',
        '};')


# Technically 'self' is not used by systemtap yet, but
# they recommended we keep it in the reserved list anyway
RESERVED_WORDS = (
    'break', 'catch', 'continue', 'delete', 'else', 'for',
    'foreach', 'function', 'global', 'if', 'in', 'limit',
    'long', 'next', 'probe', 'return', 'self', 'string',
    'try', 'while'
    )

def stap(events):
    for e in events:
        # Define prototype for probe arguments
        out('probe %(probeprefix)s.%(name)s = process("%(binary)s").mark("%(name)s")',
            '{',
            probeprefix = _probeprefix(),
            name = e.name,
            binary = _binary(),
            )

        i = 1
        if len(e.args) > 0:
            for name in e.args.names():
                # Append underscore to reserved keywords
                if name in RESERVED_WORDS:
                    name += '_'
                out('  %s = $arg%d;' % (name, i))
                i += 1

        out('}')

    out()
