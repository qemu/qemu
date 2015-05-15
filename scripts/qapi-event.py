#
# QAPI event generator
#
# Copyright (c) 2014 Wenchao Xia
#
# Authors:
#  Wenchao Xia <wenchaoqemu@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *

def _generate_event_api_name(event_name, params):
    api_name = "void qapi_event_send_%s(" % c_name(event_name).lower();
    l = len(api_name)

    if params:
        for argname, argentry, optional in parse_args(params):
            if optional:
                api_name += "bool has_%s,\n" % c_name(argname)
                api_name += "".ljust(l)

            api_name += "%s %s,\n" % (c_type(argentry, is_param=True),
                                      c_name(argname))
            api_name += "".ljust(l)

    api_name += "Error **errp)"
    return api_name;


# Following are the core functions that generate C APIs to emit event.

def generate_event_declaration(api_name):
    return mcgen('''

%(api_name)s;
''',
                 api_name = api_name)

def generate_event_implement(api_name, event_name, params):
    # step 1: declare any variables
    ret = mcgen("""

%(api_name)s
{
    QDict *qmp;
    Error *local_err = NULL;
    QMPEventFuncEmit emit;
""",
                api_name = api_name)

    if params:
        ret += mcgen("""
    QmpOutputVisitor *qov;
    Visitor *v;
    QObject *obj;

""")

    # step 2: check emit function, create a dict
    ret += mcgen("""
    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("%(event_name)s");

""",
                 event_name = event_name)

    # step 3: visit the params if params != None
    if params:
        ret += mcgen("""
    qov = qmp_output_visitor_new();
    g_assert(qov);

    v = qmp_output_get_visitor(qov);
    g_assert(v);

    /* Fake visit, as if all members are under a structure */
    visit_start_struct(v, NULL, "", "%(event_name)s", 0, &local_err);
    if (local_err) {
        goto clean;
    }

""",
                event_name = event_name)

        for argname, argentry, optional in parse_args(params):
            if optional:
                ret += mcgen("""
    if (has_%(var)s) {
""",
                             var = c_name(argname))
                push_indent()

            if argentry == "str":
                var_type = "(char **)"
            else:
                var_type = ""

            ret += mcgen("""
    visit_type_%(type)s(v, %(var_type)s&%(var)s, "%(name)s", &local_err);
    if (local_err) {
        goto clean;
    }
""",
                         var_type = var_type,
                         var = c_name(argname),
                         type = type_name(argentry),
                         name = argname)

            if optional:
                pop_indent()
                ret += mcgen("""
    }
""")

        ret += mcgen("""

    visit_end_struct(v, &local_err);
    if (local_err) {
        goto clean;
    }

    obj = qmp_output_get_qobject(qov);
    g_assert(obj != NULL);

    qdict_put_obj(qmp, "data", obj);
""")

    # step 4: call qmp event api
    ret += mcgen("""
    emit(%(event_enum_value)s, qmp, &local_err);

""",
                 event_enum_value = event_enum_value)

    # step 5: clean up
    if params:
        ret += mcgen("""
 clean:
    qmp_output_visitor_cleanup(qov);
""")
    ret += mcgen("""
    error_propagate(errp, local_err);
    QDECREF(qmp);
}
""")

    return ret


# Following are the functions that generate an enum type for all defined
# events, similar to qapi-types.py. Here we already have enum name and
# values which were generated before and recorded in event_enum_*. It also
# works around the issue that "import qapi-types" can't work.

def generate_event_enum_decl(event_enum_name, event_enum_values):
    lookup_decl = mcgen('''

extern const char *%(event_enum_name)s_lookup[];
''',
                        event_enum_name = event_enum_name)

    enum_decl = mcgen('''
typedef enum %(event_enum_name)s
{
''',
                      event_enum_name = event_enum_name)

    # append automatically generated _MAX value
    enum_max_value = c_enum_const(event_enum_name, "MAX")
    enum_values = event_enum_values + [ enum_max_value ]

    i = 0
    for value in enum_values:
        enum_decl += mcgen('''
    %(value)s = %(i)d,
''',
                     value = value,
                     i = i)
        i += 1

    enum_decl += mcgen('''
} %(event_enum_name)s;
''',
                       event_enum_name = event_enum_name)

    return lookup_decl + enum_decl

def generate_event_enum_lookup(event_enum_name, event_enum_strings):
    ret = mcgen('''

const char *%(event_enum_name)s_lookup[] = {
''',
                event_enum_name = event_enum_name)

    i = 0
    for string in event_enum_strings:
        ret += mcgen('''
    "%(string)s",
''',
                     string = string)

    ret += mcgen('''
    NULL,
};
''')
    return ret

(input_file, output_dir, do_c, do_h, prefix, dummy) = parse_command_line()

c_comment = '''
/*
 * schema-defined QAPI event functions
 *
 * Copyright (c) 2014 Wenchao Xia
 *
 * Authors:
 *  Wenchao Xia   <wenchaoqemu@gmail.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''
h_comment = '''
/*
 * schema-defined QAPI event functions
 *
 * Copyright (c) 2014 Wenchao Xia
 *
 * Authors:
 *  Wenchao Xia  <wenchaoqemu@gmail.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''

(fdef, fdecl) = open_output(output_dir, do_c, do_h, prefix,
                            'qapi-event.c', 'qapi-event.h',
                            c_comment, h_comment)

fdef.write(mcgen('''
#include "qemu-common.h"
#include "%(prefix)sqapi-event.h"
#include "%(prefix)sqapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-event.h"

''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "%(prefix)sqapi-types.h"

''',
                  prefix=prefix))

exprs = parse_schema(input_file)

event_enum_name = prefix.upper().replace('-', '_') + "QAPIEvent"
event_enum_values = []
event_enum_strings = []

for expr in exprs:
    if expr.has_key('event'):
        event_name = expr['event']
        params = expr.get('data')
        if params and len(params) == 0:
            params = None

        api_name = _generate_event_api_name(event_name, params)
        ret = generate_event_declaration(api_name)
        fdecl.write(ret)

        # We need an enum value per event
        event_enum_value = c_enum_const(event_enum_name, event_name)
        ret = generate_event_implement(api_name, event_name, params)
        fdef.write(ret)

        # Record it, and generate enum later
        event_enum_values.append(event_enum_value)
        event_enum_strings.append(event_name)

ret = generate_event_enum_decl(event_enum_name, event_enum_values)
fdecl.write(ret)
ret = generate_event_enum_lookup(event_enum_name, event_enum_strings)
fdef.write(ret)

close_output(fdef, fdecl)
