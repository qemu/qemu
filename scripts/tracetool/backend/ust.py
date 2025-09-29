# SPDX-License-Identifier: GPL-2.0-or-later

"""
LTTng User Space Tracing backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    header = 'trace-ust-' + group + '.h'
    out('#include <lttng/tracepoint.h>',
        '#include "%s"' % header,
        '',
        '/* tracepoint_enabled() was introduced in LTTng UST 2.7 */',
        '#ifndef tracepoint_enabled',
        '#define tracepoint_enabled(a, b) true',
        '#endif',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('    tracepoint(qemu, %(name)s%(tp_args)s);',
        name=event.name,
        tp_args=argnames)


def generate_h_backend_dstate(event, group):
    out('    tracepoint_enabled(qemu, %(name)s) || \\',
        name=event.name)
