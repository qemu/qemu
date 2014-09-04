#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
DTrace/SystemTAP backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
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


def generate_h_begin(events):
    out('#include "trace/generated-tracers-dtrace.h"',
        '')


def generate_h(event):
    out('    QEMU_%(uppername)s(%(argnames)s);',
        uppername=event.name.upper(),
        argnames=", ".join(event.args.names()))
