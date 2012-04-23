#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Stderr built-in backend.
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
    out('#include <stdio.h>',
        '#include "trace/stderr.h"',
        '',
        'extern TraceEvent trace_list[];')

    for num, e in enumerate(events):
        argnames = ", ".join(e.args.names())
        if len(e.args) > 0:
            argnames = ", " + argnames

        out('static inline void trace_%(name)s(%(args)s)',
            '{',
            '    if (trace_list[%(event_num)s].state != 0) {',
            '        fprintf(stderr, "%(name)s " %(fmt)s "\\n" %(argnames)s);',
            '    }',
            '}',
            name = e.name,
            args = e.args,
            event_num = num,
            fmt = e.fmt,
            argnames = argnames,
            )

    out('',
        '#define NR_TRACE_EVENTS %d' % len(events))
