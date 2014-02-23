#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Simple built-in backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


PUBLIC = True


def is_string(arg):
    strtype = ('const char*', 'char*', 'const char *', 'char *')
    if arg.lstrip().startswith(strtype):
        return True
    else:
        return False

def c(events):
    out('#include "trace.h"',
        '#include "trace/control.h"',
        '#include "trace/simple.h"',
        '',
        )

    for num, event in enumerate(events):
        out('void %(api)s(%(args)s)',
            '{',
            '    TraceBufferRecord rec;',
            api = event.api(),
            args = event.args,
            )
        sizes = []
        for type_, name in event.args:
            if is_string(type_):
                out('    size_t arg%(name)s_len = %(name)s ? MIN(strlen(%(name)s), MAX_TRACE_STRLEN) : 0;',
                    name = name,
                   )
                strsizeinfo = "4 + arg%s_len" % name
                sizes.append(strsizeinfo)
            else:
                sizes.append("8")
        sizestr = " + ".join(sizes)
        if len(event.args) == 0:
            sizestr = '0'


        out('',
            '    TraceEvent *eventp = trace_event_id(%(event_enum)s);',
            '    bool _state = trace_event_get_state_dynamic(eventp);',
            '    if (!_state) {',
            '        return;',
            '    }',
            '',
            '    if (trace_record_start(&rec, %(event_id)s, %(size_str)s)) {',
            '        return; /* Trace Buffer Full, Event Dropped ! */',
            '    }',
            event_enum = 'TRACE_' + event.name.upper(),
            event_id = num,
            size_str = sizestr,
            )

        if len(event.args) > 0:
            for type_, name in event.args:
                # string
                if is_string(type_):
                    out('    trace_record_write_str(&rec, %(name)s, arg%(name)s_len);',
                        name = name,
                       )
                # pointer var (not string)
                elif type_.endswith('*'):
                    out('    trace_record_write_u64(&rec, (uintptr_t)(uint64_t *)%(name)s);',
                        name = name,
                       )
                # primitive data type
                else:
                    out('    trace_record_write_u64(&rec, (uint64_t)%(name)s);',
                       name = name,
                       )

        out('    trace_record_finish(&rec);',
            '}',
            '')


def h(events):
    for event in events:
        out('void %(api)s(%(args)s);',
            api = event.api(),
            args = event.args,
            )
