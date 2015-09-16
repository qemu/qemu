#
# QAPI event generator
#
# Copyright (c) 2014 Wenchao Xia
# Copyright (c) 2015 Red Hat Inc.
#
# Authors:
#  Wenchao Xia <wenchaoqemu@gmail.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *

def _generate_event_api_name(event_name, params):
    api_name = "void qapi_event_send_%s(" % c_name(event_name).lower();
    l = len(api_name)

    if params:
        for m in params.members:
            if m.optional:
                api_name += "bool has_%s,\n" % c_name(m.name)
                api_name += "".ljust(l)

            api_name += "%s %s,\n" % (m.type.c_type(is_param=True),
                                      c_name(m.name))
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

    if params and params.members:
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
    if params and params.members:
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

        for memb in params.members:
            if memb.optional:
                ret += mcgen("""
    if (has_%(var)s) {
""",
                             var=c_name(memb.name))
                push_indent()

            if memb.type.name == "str":
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
                         var=c_name(memb.name),
                         type=memb.type.c_name(),
                         name=memb.name)

            if memb.optional:
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
                 event_enum_value = c_enum_const(event_enum_name, event_name))

    # step 5: clean up
    if params and params.members:
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


class QAPISchemaGenEventVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._event_names = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._event_names = []

    def visit_end(self):
        self.decl += generate_enum(event_enum_name, self._event_names)
        self.defn += generate_enum_lookup(event_enum_name, self._event_names)
        self._event_names = None

    def visit_event(self, name, info, arg_type):
        api_name = _generate_event_api_name(name, arg_type)
        self.decl += generate_event_declaration(api_name)
        self.defn += generate_event_implement(api_name, name, arg_type)
        self._event_names.append(name)


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

event_enum_name = c_name(prefix + "QAPIEvent", protect=False)

schema = QAPISchema(input_file)
gen = QAPISchemaGenEventVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
