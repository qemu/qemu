# -*- coding: utf-8 -*-

"""
Ftrace built-in backend.
"""

__author__     = "Eiichi Tsukata <eiichi.tsukata.xh@hitachi.com>"
__copyright__  = "Copyright (C) 2013 Hitachi, Ltd."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


import os.path

from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    out('#include "trace/ftrace.h"',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('    {',
        '        char ftrace_buf[MAX_TRACE_STRLEN];',
        '        int unused __attribute__ ((unused));',
        '        int trlen;',
        '        if (trace_event_get_state(%(event_id)s)) {',
        '#line %(event_lineno)d "%(event_filename)s"',
        '            trlen = snprintf(ftrace_buf, MAX_TRACE_STRLEN,',
        '                             "%(name)s " %(fmt)s "\\n" %(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        '            trlen = MIN(trlen, MAX_TRACE_STRLEN - 1);',
        '            unused = write(trace_marker_fd, ftrace_buf, trlen);',
        '        }',
        '    }',
        name=event.name,
        args=event.args,
        event_id="TRACE_" + event.name.upper(),
        event_lineno=event.lineno,
        event_filename=os.path.relpath(event.filename),
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())
