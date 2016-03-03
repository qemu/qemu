#
# QAPI event generator
#
# Copyright (c) 2014 Wenchao Xia
# Copyright (c) 2015-2016 Red Hat Inc.
#
# Authors:
#  Wenchao Xia <wenchaoqemu@gmail.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *


def gen_event_send_proto(name, arg_type):
    return 'void qapi_event_send_%(c_name)s(%(param)s)' % {
        'c_name': c_name(name.lower()),
        'param': gen_params(arg_type, 'Error **errp')}


def gen_event_send_decl(name, arg_type):
    return mcgen('''

%(proto)s;
''',
                 proto=gen_event_send_proto(name, arg_type))


def gen_event_send(name, arg_type):
    ret = mcgen('''

%(proto)s
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;
''',
                proto=gen_event_send_proto(name, arg_type))

    if arg_type and arg_type.members:
        ret += mcgen('''
    QmpOutputVisitor *qov;
    Visitor *v;
    QObject *obj;

''')

    ret += mcgen('''
    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("%(name)s");

''',
                 name=name)

    if arg_type and arg_type.members:
        ret += mcgen('''
    qov = qmp_output_visitor_new();
    v = qmp_output_get_visitor(qov);

    visit_start_struct(v, "%(name)s", NULL, 0, &err);
''',
                     name=name)
        ret += gen_err_check()
        ret += gen_visit_members(arg_type.members, need_cast=True,
                                 label='out_obj')
        ret += mcgen('''
out_obj:
    visit_end_struct(v, err ? NULL : &err);
    if (err) {
        goto out;
    }

    obj = qmp_output_get_qobject(qov);
    g_assert(obj);

    qdict_put_obj(qmp, "data", obj);
''')

    ret += mcgen('''
    emit(%(c_enum)s, qmp, &err);

''',
                 c_enum=c_enum_const(event_enum_name, name))

    if arg_type and arg_type.members:
        ret += mcgen('''
out:
    qmp_output_visitor_cleanup(qov);
''')
    ret += mcgen('''
    error_propagate(errp, err);
    QDECREF(qmp);
}
''')
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
        self.decl += gen_enum(event_enum_name, self._event_names)
        self.defn += gen_enum_lookup(event_enum_name, self._event_names)
        self._event_names = None

    def visit_event(self, name, info, arg_type):
        self.decl += gen_event_send_decl(name, arg_type)
        self.defn += gen_event_send(name, arg_type)
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
#include "qemu/osdep.h"
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
