# SPDX-License-Identifier: GPL-2.0-or-later

"""
Stderr built-in backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out, expand_format_string


PUBLIC = True
CHECK_TRACE_EVENT_GET_STATE = True


def generate_h_begin(events, group):
    out('#include "qemu/log-for-trace.h"',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('        if (qemu_loglevel_mask(LOG_TRACE)) {',
        '#line %(event_lineno)d "%(event_filename)s"',
        '            qemu_log("%(name)s " %(fmt)s "\\n"%(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        '        }',
        event_lineno=event.lineno,
        event_filename=event.filename,
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())

def generate_rs(event, group):
    out('        let format_string = c"%(fmt)s\\n";',
        '        if (unsafe { bindings::qemu_loglevel } & bindings::LOG_TRACE) != 0 {',
        '            unsafe { bindings::qemu_log(format_string.as_ptr() as *const c_char, %(args)s);}',
        '        }',
        fmt=expand_format_string(event.fmt, event.name + " "),
        args=event.args.rust_call_varargs())
