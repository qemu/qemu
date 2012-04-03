#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Simple built-in backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


def c(events):
    out('#include "trace.h"',
        '',
        'TraceEvent trace_list[] = {')

    for e in events:
        out('{.tp_name = "%(name)s", .state=0},',
            name = e.name,
            )

    out('};')

def h(events):
    out('#include "trace/simple.h"',
        '')

    for num, e in enumerate(events):
        if len(e.args):
            argstr = e.args.names()
            arg_prefix = ', (uint64_t)(uintptr_t)'
            cast_args = arg_prefix + arg_prefix.join(argstr)
            simple_args = (str(num) + cast_args)
        else:
            simple_args = str(num)

        out('static inline void trace_%(name)s(%(args)s)',
            '{',
            '    trace%(argc)d(%(trace_args)s);',
            '}',
            name = e.name,
            args = e.args,
            argc = len(e.args),
            trace_args = simple_args,
            )

    out('#define NR_TRACE_EVENTS %d' % len(events))
    out('extern TraceEvent trace_list[NR_TRACE_EVENTS];')
