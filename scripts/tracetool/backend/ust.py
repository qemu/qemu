#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
LTTng User Space Tracing backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True


def c(events):
    out('#include <ust/marker.h>',
        '#undef mutex_lock',
        '#undef mutex_unlock',
        '#undef inline',
        '#undef wmb',
        '#include "trace.h"')

    for e in events:
        argnames = ", ".join(e.args.names())
        if len(e.args) > 0:
            argnames = ', ' + argnames

            out('DEFINE_TRACE(ust_%(name)s);',
                '',
                'static void ust_%(name)s_probe(%(args)s)',
                '{',
                '    trace_mark(ust, %(name)s, %(fmt)s%(argnames)s);',
                '}',
                name = e.name,
                args = e.args,
                fmt = e.fmt,
                argnames = argnames,
                )

        else:
            out('DEFINE_TRACE(ust_%(name)s);',
                '',
                'static void ust_%(name)s_probe(%(args)s)',
                '{',
                '    trace_mark(ust, %(name)s, UST_MARKER_NOARGS);',
                '}',
                name = e.name,
                args = e.args,
                )

    # register probes
    out('',
        'static void __attribute__((constructor)) trace_init(void)',
        '{')

    for e in events:
        out('    register_trace_ust_%(name)s(ust_%(name)s_probe);',
            name = e.name,
            )

    out('}')


def h(events):
    out('#include <ust/tracepoint.h>',
        '#undef mutex_lock',
        '#undef mutex_unlock',
        '#undef inline',
        '#undef wmb')

    for e in events:
        if len(e.args) > 0:
            out('DECLARE_TRACE(ust_%(name)s, TP_PROTO(%(args)s), TP_ARGS(%(argnames)s));',
                '#define trace_%(name)s trace_ust_%(name)s',
                name = e.name,
                args = e.args,
                argnames = ", ".join(e.args.names()),
                )

        else:
            out('_DECLARE_TRACEPOINT_NOARGS(ust_%(name)s);',
                '#define trace_%(name)s trace_ust_%(name)s',
                name = e.name,
                )

    out()
