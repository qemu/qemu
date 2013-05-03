#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Ftrace built-in backend.
"""

__author__     = "Eiichi Tsukata <eiichi.tsukata.xh@hitachi.com>"
__copyright__  = "Copyright (C) 2013 Hitachi, Ltd."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out


PUBLIC = True


def c(events):
    pass

def h(events):
    out('#include "trace/ftrace.h"',
        '#include "trace/control.h"',
        '',
        )

    for e in events:
        argnames = ", ".join(e.args.names())
        if len(e.args) > 0:
            argnames = ", " + argnames

        out('static inline void trace_%(name)s(%(args)s)',
            '{',
            '    char ftrace_buf[MAX_TRACE_STRLEN];',
            '    int unused __attribute__ ((unused));',
            '    int trlen;',
            '    bool _state = trace_event_get_state(%(event_id)s);',
            '    if (_state) {',
            '        trlen = snprintf(ftrace_buf, MAX_TRACE_STRLEN,',
            '                         "%(name)s " %(fmt)s "\\n" %(argnames)s);',
            '        trlen = MIN(trlen, MAX_TRACE_STRLEN - 1);',
            '        unused = write(trace_marker_fd, ftrace_buf, trlen);',
            '    }',
            '}',
            name = e.name,
            args = e.args,
            event_id = "TRACE_" + e.name.upper(),
            fmt = e.fmt.rstrip("\n"),
            argnames = argnames,
            )
