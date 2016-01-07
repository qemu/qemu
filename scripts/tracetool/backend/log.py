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


def generate_h_begin(events):
    out('#include <stdio.h>',
        '#include <sys/time.h>',
        '#include <sys/types.h>',
        '#include <unistd.h>',
        '#include "trace/control.h"',
        '#include "qemu/log.h"',
        '')


def generate_h(event):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('    if (trace_event_get_state(%(event_id)s)) {',
        '        struct timeval _now;',
        '        gettimeofday(&_now, NULL);',
        '        qemu_log_mask(LOG_TRACE, "%%d@%%zd.%%06zd:%(name)s " %(fmt)s "\\n",',
        '                      getpid(),',
        '                      (size_t)_now.tv_sec, (size_t)_now.tv_usec',
        '                      %(argnames)s);',
        '    }',
        event_id="TRACE_" + event.name.upper(),
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)
