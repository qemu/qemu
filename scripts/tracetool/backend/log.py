#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Stderr built-in backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    out('#include "qemu/log-for-trace.h"',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    if "vcpu" in event.properties:
        # already checked on the generic format code
        cond = "true"
    else:
        cond = "trace_event_get_state(%s)" % ("TRACE_" + event.name.upper())

    out('    if (%(cond)s && qemu_loglevel_mask(LOG_TRACE)) {',
        '        struct timeval _now;',
        '        gettimeofday(&_now, NULL);',
        '        qemu_log("%%d@%%zu.%%06zu:%(name)s " %(fmt)s "\\n",',
        '                 qemu_get_thread_id(),',
        '                 (size_t)_now.tv_sec, (size_t)_now.tv_usec',
        '                 %(argnames)s);',
        '    }',
        cond=cond,
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())
