#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
LTTng User Space Tracing backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2016, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    if group == "root":
        header = "trace-ust-root.h"
    else:
        header = "trace-ust.h"

    out('#include <lttng/tracepoint.h>',
        '#include "%s"' % header,
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('        tracepoint(qemu, %(name)s%(tp_args)s);',
        name=event.name,
        tp_args=argnames)
