#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Stderr built-in backend.
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
    out('#include <stdio.h>',
        '#include "trace/control.h"',
        '',
        )

    for e in events:
        argnames = ", ".join(e.args.names())
        if len(e.args) > 0:
            argnames = ", " + argnames

        out('static inline void %(api)s(%(args)s)',
            '{',
            '    bool _state = trace_event_get_state(%(event_id)s);',
            '    if (_state) {',
            '        fprintf(stderr, "%(name)s " %(fmt)s "\\n" %(argnames)s);',
            '    }',
            '}',
            api = e.api(),
            name = e.name,
            args = e.args,
            event_id = "TRACE_" + e.name.upper(),
            fmt = e.fmt.rstrip("\n"),
            argnames = argnames,
            )
