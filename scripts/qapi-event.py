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


def gen_event_send_proto(name, arg_type, boxed):
    return 'void qapi_event_send_%(c_name)s(%(param)s)' % {
        'c_name': c_name(name.lower()),
        'param': gen_params(arg_type, boxed, 'Error **errp')}


def gen_event_send_decl(name, arg_type, boxed):
    return mcgen('''

%(proto)s;
''',
                 proto=gen_event_send_proto(name, arg_type, boxed))


# Declare and initialize an object 'qapi' using parameters from gen_params()
def gen_param_var(typ):
    assert not typ.variants
    ret = mcgen('''
    %(c_name)s param = {
''',
                c_name=typ.c_name())
    sep = '        '
    for memb in typ.members:
        ret += sep
        sep = ', '
        if memb.optional:
            ret += 'has_' + c_name(memb.name) + sep
        if memb.type.name == 'str':
            # Cast away const added in gen_params()
            ret += '(char *)'
        ret += c_name(memb.name)
    ret += mcgen('''

    };
''')
    if not typ.is_implicit():
        ret += mcgen('''
    %(c_name)s *arg = &param;
''',
                     c_name=typ.c_name())
    return ret


def gen_event_send(name, arg_type, boxed):
    # FIXME: Our declaration of local variables (and of 'errp' in the
    # parameter list) can collide with exploded members of the event's
    # data type passed in as parameters.  If this collision ever hits in
    # practice, we can rename our local variables with a leading _ prefix,
    # or split the code into a wrapper function that creates a boxed
    # 'param' object then calls another to do the real work.
    ret = mcgen('''

%(proto)s
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;
''',
                proto=gen_event_send_proto(name, arg_type, boxed))

    if arg_type and not arg_type.is_empty():
        ret += mcgen('''
    QObject *obj;
    Visitor *v;
''')
        if not boxed:
            ret += gen_param_var(arg_type)
    else:
        assert not boxed

    ret += mcgen('''

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("%(name)s");

''',
                 name=name)

    if arg_type and not arg_type.is_empty():
        ret += mcgen('''
    v = qobject_output_visitor_new(&obj);
''')
        if not arg_type.is_implicit():
            ret += mcgen('''
    visit_type_%(c_name)s(v, "%(name)s", &arg, &err);
''',
                         name=name, c_name=arg_type.c_name())
        else:
            ret += mcgen('''

    visit_start_struct(v, "%(name)s", NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_%(c_name)s_members(v, &param, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v, NULL);
''',
                         name=name, c_name=arg_type.c_name())
        ret += mcgen('''
    if (err) {
        goto out;
    }

    visit_complete(v, &obj);
    qdict_put_obj(qmp, "data", obj);
''')

    ret += mcgen('''
    emit(%(c_enum)s, qmp, &err);

''',
                 c_enum=c_enum_const(event_enum_name, name))

    if arg_type and not arg_type.is_empty():
        ret += mcgen('''
out:
    visit_free(v);
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

    def visit_event(self, name, info, arg_type, boxed):
        self.decl += gen_event_send_decl(name, arg_type, boxed)
        self.defn += gen_event_send(name, arg_type, boxed)
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
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp-event.h"

''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "%(prefix)sqapi-types.h"

''',
                  prefix=prefix))

event_enum_name = c_name(prefix + 'QAPIEvent', protect=False)

schema = QAPISchema(input_file)
gen = QAPISchemaGenEventVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
