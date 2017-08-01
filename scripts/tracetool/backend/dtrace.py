#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
DTrace/SystemTAP backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True


PROBEPREFIX = None

def probeprefix():
    if PROBEPREFIX is None:
        raise ValueError("you must set PROBEPREFIX")
    return PROBEPREFIX


BINARY = None

def binary():
    if BINARY is None:
        raise ValueError("you must set BINARY")
    return BINARY


def generate_h_begin(events, group):
    if group == "root":
        header = "trace-dtrace-root.h"
    else:
        header = "trace-dtrace.h"

    out('#include "%s"' % header,
        '')

    # SystemTap defines <provider>_<name>_ENABLED() but other DTrace
    # implementations might not.
    for e in events:
        out('#ifndef QEMU_%(uppername)s_ENABLED',
            '#define QEMU_%(uppername)s_ENABLED() true',
            '#endif',
            uppername=e.name.upper())

def generate_h(event, group):
    out('    QEMU_%(uppername)s(%(argnames)s);',
        uppername=event.name.upper(),
        argnames=", ".join(event.args.names()))


def generate_h_backend_dstate(event, group):
    out('    QEMU_%(uppername)s_ENABLED() || \\',
        uppername=event.name.upper())
