#
# QAPI command marshaller generator
#
# Copyright IBM, Corp. 2011
# Copyright (C) 2014-2016 Red Hat, Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Michael Roth    <mdroth@linux.vnet.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *
import re


def gen_command_decl(name, arg_type, ret_type):
    return mcgen('''
%(c_type)s qmp_%(c_name)s(%(params)s);
''',
                 c_type=(ret_type and ret_type.c_type()) or 'void',
                 c_name=c_name(name),
                 params=gen_params(arg_type, 'Error **errp'))


def gen_call(name, arg_type, ret_type):
    ret = ''

    argstr = ''
    if arg_type:
        assert not arg_type.variants
        for memb in arg_type.members:
            if memb.optional:
                argstr += 'arg.has_%s, ' % c_name(memb.name)
            argstr += 'arg.%s, ' % c_name(memb.name)

    lhs = ''
    if ret_type:
        lhs = 'retval = '

    ret = mcgen('''

    %(lhs)sqmp_%(c_name)s(%(args)s&err);
''',
                c_name=c_name(name), args=argstr, lhs=lhs)
    if ret_type:
        ret += gen_err_check()
        ret += mcgen('''

    qmp_marshal_output_%(c_name)s(retval, ret, &err);
''',
                     c_name=ret_type.c_name())
    return ret


def gen_marshal_output(ret_type):
    return mcgen('''

static void qmp_marshal_output_%(c_name)s(%(c_type)s ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type_%(c_name)s(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_%(c_name)s(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}
''',
                 c_type=ret_type.c_type(), c_name=ret_type.c_name())


def gen_marshal_proto(name):
    ret = 'void qmp_marshal_%s(QDict *args, QObject **ret, Error **errp)' % c_name(name)
    if not middle_mode:
        ret = 'static ' + ret
    return ret


def gen_marshal_decl(name):
    return mcgen('''
%(proto)s;
''',
                 proto=gen_marshal_proto(name))


def gen_marshal(name, arg_type, ret_type):
    ret = mcgen('''

%(proto)s
{
    Error *err = NULL;
''',
                proto=gen_marshal_proto(name))

    if ret_type:
        ret += mcgen('''
    %(c_type)s retval;
''',
                     c_type=ret_type.c_type())

    if arg_type and arg_type.members:
        ret += mcgen('''
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    %(c_name)s arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_type_%(c_name)s_members(v, &arg, &err);
    if (err) {
        goto out;
    }
''',
                     c_name=arg_type.c_name())

    else:
        ret += mcgen('''

    (void)args;
''')

    ret += gen_call(name, arg_type, ret_type)

    # 'goto out' produced above for arg_type, and by gen_call() for ret_type
    if (arg_type and arg_type.members) or ret_type:
        ret += mcgen('''

out:
''')
    ret += mcgen('''
    error_propagate(errp, err);
''')
    if arg_type and arg_type.members:
        ret += mcgen('''
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_%(c_name)s_members(v, &arg, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
''',
                     c_name=arg_type.c_name())

    ret += mcgen('''
}
''')
    return ret


def gen_register_command(name, success_response):
    options = 'QCO_NO_OPTIONS'
    if not success_response:
        options = 'QCO_NO_SUCCESS_RESP'

    ret = mcgen('''
    qmp_register_command("%(name)s", qmp_marshal_%(c_name)s, %(opts)s);
''',
                name=name, c_name=c_name(name),
                opts=options)
    return ret


def gen_registry(registry):
    ret = mcgen('''

static void qmp_init_marshal(void)
{
''')
    ret += registry
    ret += mcgen('''
}

qapi_init(qmp_init_marshal);
''')
    return ret


class QAPISchemaGenCommandVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._regy = None
        self._visited_ret_types = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._regy = ''
        self._visited_ret_types = set()

    def visit_end(self):
        if not middle_mode:
            self.defn += gen_registry(self._regy)
        self._regy = None
        self._visited_ret_types = None

    def visit_command(self, name, info, arg_type, ret_type,
                      gen, success_response):
        if not gen:
            return
        self.decl += gen_command_decl(name, arg_type, ret_type)
        if ret_type and ret_type not in self._visited_ret_types:
            self._visited_ret_types.add(ret_type)
            self.defn += gen_marshal_output(ret_type)
        if middle_mode:
            self.decl += gen_marshal_decl(name)
        self.defn += gen_marshal(name, arg_type, ret_type)
        if not middle_mode:
            self._regy += gen_register_command(name, success_response)


middle_mode = False

(input_file, output_dir, do_c, do_h, prefix, opts) = \
    parse_command_line("m", ["middle"])

for o, a in opts:
    if o in ("-m", "--middle"):
        middle_mode = True

c_comment = '''
/*
 * schema-defined QMP->QAPI command dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''
h_comment = '''
/*
 * schema-defined QAPI function prototypes
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''

(fdef, fdecl) = open_output(output_dir, do_c, do_h, prefix,
                            'qmp-marshal.c', 'qmp-commands.h',
                            c_comment, h_comment)

fdef.write(mcgen('''
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/module.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
#include "%(prefix)sqmp-commands.h"

''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "%(prefix)sqapi-types.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"

''',
                  prefix=prefix))

schema = QAPISchema(input_file)
gen = QAPISchemaGenCommandVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
