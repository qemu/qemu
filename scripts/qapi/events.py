"""
QAPI event generator

Copyright (c) 2014 Wenchao Xia
Copyright (c) 2015-2018 Red Hat Inc.

Authors:
 Wenchao Xia <wenchaoqemu@gmail.com>
 Markus Armbruster <armbru@redhat.com>

This work is licensed under the terms of the GNU GPL, version 2.
See the COPYING file in the top-level directory.
"""

from typing import List, Optional

from .common import c_enum_const, c_name, mcgen
from .gen import QAPISchemaModularCVisitor, build_params, ifcontext
from .schema import (
    QAPISchema,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
)
from .source import QAPISourceInfo
from .types import gen_enum, gen_enum_lookup


def build_event_send_proto(name: str,
                           arg_type: Optional[QAPISchemaObjectType],
                           boxed: bool) -> str:
    return 'void qapi_event_send_%(c_name)s(%(param)s)' % {
        'c_name': c_name(name.lower()),
        'param': build_params(arg_type, boxed)}


def gen_event_send_decl(name: str,
                        arg_type: Optional[QAPISchemaObjectType],
                        boxed: bool) -> str:
    return mcgen('''

%(proto)s;
''',
                 proto=build_event_send_proto(name, arg_type, boxed))


def gen_param_var(typ: QAPISchemaObjectType) -> str:
    """
    Generate a struct variable holding the event parameters.

    Initialize it with the function arguments defined in `gen_event_send`.
    """
    assert not typ.variants
    ret = mcgen('''
    %(c_name)s param = {
''',
                c_name=typ.c_name())
    sep = '        '
    for memb in typ.members:
        ret += sep
        sep = ', '
        if memb.need_has():
            ret += 'has_' + c_name(memb.name) + sep
        if memb.type.name == 'str':
            # Cast away const added in build_params()
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


def gen_event_send(name: str,
                   arg_type: Optional[QAPISchemaObjectType],
                   features: List[QAPISchemaFeature],
                   boxed: bool,
                   event_enum_name: str,
                   event_emit: str) -> str:
    # FIXME: Our declaration of local variables (and of 'errp' in the
    # parameter list) can collide with exploded members of the event's
    # data type passed in as parameters.  If this collision ever hits in
    # practice, we can rename our local variables with a leading _ prefix,
    # or split the code into a wrapper function that creates a boxed
    # 'param' object then calls another to do the real work.
    have_args = boxed or (arg_type and not arg_type.is_empty())

    ret = mcgen('''

%(proto)s
{
    QDict *qmp;
''',
                proto=build_event_send_proto(name, arg_type, boxed))

    if have_args:
        assert arg_type is not None
        ret += mcgen('''
    QObject *obj;
    Visitor *v;
''')
        if not boxed:
            ret += gen_param_var(arg_type)

    for f in features:
        if f.is_special():
            ret += mcgen('''

    if (compat_policy.%(feat)s_output == COMPAT_POLICY_OUTPUT_HIDE) {
        return;
    }
''',
                         feat=f.name)

    ret += mcgen('''

    qmp = qmp_event_build_dict("%(name)s");

''',
                 name=name)

    if have_args:
        assert arg_type is not None
        ret += mcgen('''
    v = qobject_output_visitor_new_qmp(&obj);
''')
        if not arg_type.is_implicit():
            ret += mcgen('''
    visit_type_%(c_name)s(v, "%(name)s", &arg, &error_abort);
''',
                         name=name, c_name=arg_type.c_name())
        else:
            ret += mcgen('''

    visit_start_struct(v, "%(name)s", NULL, 0, &error_abort);
    visit_type_%(c_name)s_members(v, &param, &error_abort);
    visit_check_struct(v, &error_abort);
    visit_end_struct(v, NULL);
''',
                         name=name, c_name=arg_type.c_name())
        ret += mcgen('''

    visit_complete(v, &obj);
    if (qdict_size(qobject_to(QDict, obj))) {
        qdict_put_obj(qmp, "data", obj);
    } else {
        qobject_unref(obj);
    }
''')

    ret += mcgen('''
    %(event_emit)s(%(c_enum)s, qmp);

''',
                 event_emit=event_emit,
                 c_enum=c_enum_const(event_enum_name, name))

    if have_args:
        ret += mcgen('''
    visit_free(v);
''')
    ret += mcgen('''
    qobject_unref(qmp);
}
''')
    return ret


class QAPISchemaGenEventVisitor(QAPISchemaModularCVisitor):

    def __init__(self, prefix: str):
        super().__init__(
            prefix, 'qapi-events',
            ' * Schema-defined QAPI/QMP events', None, __doc__)
        self._event_enum_name = c_name(prefix + 'QAPIEvent', protect=False)
        self._event_enum_members: List[QAPISchemaEnumMember] = []
        self._event_emit_name = c_name(prefix + 'qapi_event_emit')

    def _begin_user_module(self, name: str) -> None:
        events = self._module_basename('qapi-events', name)
        types = self._module_basename('qapi-types', name)
        visit = self._module_basename('qapi-visit', name)
        self._genc.add(mcgen('''
#include "qemu/osdep.h"
#include "%(prefix)sqapi-emit-events.h"
#include "%(events)s.h"
#include "%(visit)s.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp-event.h"
''',
                             events=events, visit=visit,
                             prefix=self._prefix))
        self._genh.add(mcgen('''
#include "qapi/util.h"
#include "%(types)s.h"
''',
                             types=types))

    def visit_end(self) -> None:
        self._add_module('./emit', ' * QAPI Events emission')
        self._genc.preamble_add(mcgen('''
#include "qemu/osdep.h"
#include "%(prefix)sqapi-emit-events.h"
''',
                                      prefix=self._prefix))
        self._genh.preamble_add(mcgen('''
#include "qapi/util.h"
'''))
        self._genh.add(gen_enum(self._event_enum_name,
                                self._event_enum_members))
        self._genc.add(gen_enum_lookup(self._event_enum_name,
                                       self._event_enum_members))
        self._genh.add(mcgen('''

void %(event_emit)s(%(event_enum)s event, QDict *qdict);
''',
                             event_emit=self._event_emit_name,
                             event_enum=self._event_enum_name))

    def visit_event(self,
                    name: str,
                    info: Optional[QAPISourceInfo],
                    ifcond: QAPISchemaIfCond,
                    features: List[QAPISchemaFeature],
                    arg_type: Optional[QAPISchemaObjectType],
                    boxed: bool) -> None:
        with ifcontext(ifcond, self._genh, self._genc):
            self._genh.add(gen_event_send_decl(name, arg_type, boxed))
            self._genc.add(gen_event_send(name, arg_type, features, boxed,
                                          self._event_enum_name,
                                          self._event_emit_name))
        # Note: we generate the enum member regardless of @ifcond, to
        # keep the enumeration usable in target-independent code.
        self._event_enum_members.append(QAPISchemaEnumMember(name, None))


def gen_events(schema: QAPISchema,
               output_dir: str,
               prefix: str) -> None:
    vis = QAPISchemaGenEventVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
