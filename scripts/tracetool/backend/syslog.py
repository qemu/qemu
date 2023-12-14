# -*- coding: utf-8 -*-

"""
Syslog built-in backend.
"""

__author__     = "Paul Durrant <paul.durrant@citrix.com>"
__copyright__  = "Copyright 2016, Citrix Systems Inc."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


import os.path

from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    out('#include <syslog.h>',
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

    out('    if (%(cond)s) {',
        '#line %(event_lineno)d "%(event_filename)s"',
        '        syslog(LOG_INFO, "%(name)s " %(fmt)s %(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        '    }',
        cond=cond,
        event_lineno=event.lineno,
        event_filename=os.path.relpath(event.filename),
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())
