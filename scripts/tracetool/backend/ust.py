#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
LTTng User Space Tracing backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True

def c(events):
    pass


def h(events):
    out('#include <lttng/tracepoint.h>',
        '#include "trace/generated-ust-provider.h"',
        '')
    for e in events:
        argnames = ", ".join(e.args.names())
        if len(e.args) > 0:
            argnames = ", " + argnames

        out('static inline void %(api)s(%(args)s)',
            '{',
            '    tracepoint(qemu, %(name)s%(tp_args)s);',
            '}',
            '',
            api = e.api()
            name = e.name,
            args = e.args,
            tp_args = argnames,
            )

def ust_events_c(events):
    pass

def ust_events_h(events):
    for e in events:
        if len(e.args) > 0:
            out('TRACEPOINT_EVENT(',
                '   qemu,',
                '   %(name)s,',
                '   TP_ARGS(%(args)s),',
                '   TP_FIELDS(',
                name = e.name,
                args = ", ".join(", ".join(i) for i in e.args),
                )

            for t,n in e.args:
                if ('int' in t) or ('long' in t) or ('unsigned' in t) or ('size_t' in t):
                    out('       ctf_integer(' + t + ', ' + n + ', ' + n + ')')
                elif ('double' in t) or ('float' in t):
                    out('       ctf_float(' + t + ', ' + n + ', ' + n + ')')
                elif ('char *' in t) or ('char*' in t):
                    out('       ctf_string(' + n + ', ' + n + ')')
                elif ('void *' in t) or ('void*' in t):
                    out('       ctf_integer_hex(unsigned long, ' + n + ', ' + n + ')')

            out('   )',
                ')',
                '')

        else:
            out('TRACEPOINT_EVENT(',
                '   qemu,',
                '   %(name)s,',
                '   TP_ARGS(void),',
                '   TP_FIELDS()',
                ')',
                '',
                name = e.name,
                )
