# SPDX-License-Identifier: GPL-2.0-or-later

"""
Syslog built-in backend.
"""

__author__     = "Paul Durrant <paul.durrant@citrix.com>"
__copyright__  = "Copyright 2016, Citrix Systems Inc."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out, expand_format_string


PUBLIC = True
CHECK_TRACE_EVENT_GET_STATE = True


def generate_h_begin(events, group):
    out('#include <syslog.h>',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('#line %(event_lineno)d "%(event_filename)s"',
        '        syslog(LOG_INFO, "%(name)s " %(fmt)s %(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        event_lineno=event.lineno,
        event_filename=event.filename,
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)

def generate_rs(event, group):
    out('        let format_string = c"%(fmt)s";',
        '        unsafe {::trace::syslog(::trace::LOG_INFO, format_string.as_ptr() as *const c_char, %(args)s);}',
        fmt=expand_format_string(event.fmt),
        args=event.args.rust_call_varargs())

def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())
