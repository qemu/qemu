#
# QAPI visitor generator
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


def gen_visit_decl(name, scalar=False):
    c_type = c_name(name) + ' *'
    if not scalar:
        c_type += '*'
    return mcgen('''
void visit_type_%(c_name)s(Visitor *v, const char *name, %(c_type)sobj, Error **errp);
''',
                 c_name=c_name(name), c_type=c_type)


def gen_visit_members_decl(name):
    return mcgen('''

void visit_type_%(c_name)s_members(Visitor *v, %(c_name)s *obj, Error **errp);
''',
                 c_name=c_name(name))


def gen_visit_object_members(name, base, members, variants):
    ret = mcgen('''

void visit_type_%(c_name)s_members(Visitor *v, %(c_name)s *obj, Error **errp)
{
    Error *err = NULL;

''',
                c_name=c_name(name))

    if base:
        ret += mcgen('''
    visit_type_%(c_type)s_members(v, (%(c_type)s *)obj, &err);
''',
                     c_type=base.c_name())
        ret += gen_err_check()

    for memb in members:
        if memb.optional:
            ret += mcgen('''
    if (visit_optional(v, "%(name)s", &obj->has_%(c_name)s)) {
''',
                         name=memb.name, c_name=c_name(memb.name))
            push_indent()
        ret += mcgen('''
    visit_type_%(c_type)s(v, "%(name)s", &obj->%(c_name)s, &err);
''',
                     c_type=memb.type.c_name(), name=memb.name,
                     c_name=c_name(memb.name))
        ret += gen_err_check()
        if memb.optional:
            pop_indent()
            ret += mcgen('''
    }
''')

    if variants:
        ret += mcgen('''
    switch (obj->%(c_name)s) {
''',
                     c_name=c_name(variants.tag_member.name))

        for var in variants.variants:
            ret += mcgen('''
    case %(case)s:
        visit_type_%(c_type)s_members(v, &obj->u.%(c_name)s, &err);
        break;
''',
                         case=c_enum_const(variants.tag_member.type.name,
                                           var.name,
                                           variants.tag_member.type.prefix),
                         c_type=var.type.c_name(), c_name=c_name(var.name))

        ret += mcgen('''
    default:
        abort();
    }
''')

    # 'goto out' produced for base, for each member, and if variants were
    # present
    if base or members or variants:
        ret += mcgen('''

out:
''')
    ret += mcgen('''
    error_propagate(errp, err);
}
''')
    return ret


def gen_visit_list(name, element_type):
    # FIXME: if *obj is NULL on entry, and the first visit_next_list()
    # assigns to *obj, while a later one fails, we should clean up *obj
    # rather than leaving it non-NULL. As currently written, the caller must
    # call qapi_free_FOOList() to avoid a memory leak of the partial FOOList.
    return mcgen('''

void visit_type_%(c_name)s(Visitor *v, const char *name, %(c_name)s **obj, Error **errp)
{
    Error *err = NULL;
    %(c_name)s *tail;
    size_t size = sizeof(**obj);

    visit_start_list(v, name, (GenericList **)obj, size, &err);
    if (err) {
        goto out;
    }

    for (tail = *obj; tail;
         tail = (%(c_name)s *)visit_next_list(v, (GenericList *)tail, size)) {
        visit_type_%(c_elt_type)s(v, NULL, &tail->value, &err);
        if (err) {
            break;
        }
    }

    visit_end_list(v);
out:
    error_propagate(errp, err);
}
''',
                 c_name=c_name(name), c_elt_type=element_type.c_name())


def gen_visit_enum(name):
    return mcgen('''

void visit_type_%(c_name)s(Visitor *v, const char *name, %(c_name)s *obj, Error **errp)
{
    int value = *obj;
    visit_type_enum(v, name, &value, %(c_name)s_lookup, errp);
    *obj = value;
}
''',
                 c_name=c_name(name))


def gen_visit_alternate(name, variants):
    promote_int = 'true'
    ret = ''
    for var in variants.variants:
        if var.type.alternate_qtype() == 'QTYPE_QINT':
            promote_int = 'false'

    ret += mcgen('''

void visit_type_%(c_name)s(Visitor *v, const char *name, %(c_name)s **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_alternate(v, name, (GenericAlternate **)obj, sizeof(**obj),
                          %(promote_int)s, &err);
    if (err) {
        goto out;
    }
    switch ((*obj)->type) {
''',
                 c_name=c_name(name), promote_int=promote_int)

    for var in variants.variants:
        ret += mcgen('''
    case %(case)s:
''',
                     case=var.type.alternate_qtype())
        if isinstance(var.type, QAPISchemaObjectType):
            ret += mcgen('''
        visit_start_struct(v, name, NULL, 0, &err);
        if (err) {
            break;
        }
        visit_type_%(c_type)s_members(v, &(*obj)->u.%(c_name)s, &err);
        if (!err) {
            visit_check_struct(v, &err);
        }
        visit_end_struct(v);
''',
                         c_type=var.type.c_name(),
                         c_name=c_name(var.name))
        else:
            ret += mcgen('''
        visit_type_%(c_type)s(v, name, &(*obj)->u.%(c_name)s, &err);
''',
                         c_type=var.type.c_name(),
                         c_name=c_name(var.name))
        ret += mcgen('''
        break;
''')

    ret += mcgen('''
    default:
        error_setg(&err, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "%(name)s");
    }
    visit_end_alternate(v);
out:
    error_propagate(errp, err);
}
''',
                 name=name)

    return ret


def gen_visit_object(name, base, members, variants):
    # FIXME: if *obj is NULL on entry, and visit_start_struct() assigns to
    # *obj, but then visit_type_FOO_members() fails, we should clean up *obj
    # rather than leaving it non-NULL. As currently written, the caller must
    # call qapi_free_FOO() to avoid a memory leak of the partial FOO.
    return mcgen('''

void visit_type_%(c_name)s(Visitor *v, const char *name, %(c_name)s **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(%(c_name)s), &err);
    if (err) {
        goto out;
    }
    if (!*obj) {
        goto out_obj;
    }
    visit_type_%(c_name)s_members(v, *obj, &err);
    if (err) {
        goto out_obj;
    }
    visit_check_struct(v, &err);
out_obj:
    visit_end_struct(v);
out:
    error_propagate(errp, err);
}
''',
                 c_name=c_name(name))


class QAPISchemaGenVisitVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._btin = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._btin = guardstart('QAPI_VISIT_BUILTIN')

    def visit_end(self):
        # To avoid header dependency hell, we always generate
        # declarations for built-in types in our header files and
        # simply guard them.  See also do_builtins (command line
        # option -b).
        self._btin += guardend('QAPI_VISIT_BUILTIN')
        self.decl = self._btin + self.decl
        self._btin = None

    def visit_enum_type(self, name, info, values, prefix):
        # Special case for our lone builtin enum type
        # TODO use something cleaner than existence of info
        if not info:
            self._btin += gen_visit_decl(name, scalar=True)
            if do_builtins:
                self.defn += gen_visit_enum(name)
        else:
            self.decl += gen_visit_decl(name, scalar=True)
            self.defn += gen_visit_enum(name)

    def visit_array_type(self, name, info, element_type):
        decl = gen_visit_decl(name)
        defn = gen_visit_list(name, element_type)
        if isinstance(element_type, QAPISchemaBuiltinType):
            self._btin += decl
            if do_builtins:
                self.defn += defn
        else:
            self.decl += decl
            self.defn += defn

    def visit_object_type(self, name, info, base, members, variants):
        # Nothing to do for the special empty builtin
        if name == 'q_empty':
            return
        self.decl += gen_visit_members_decl(name)
        self.defn += gen_visit_object_members(name, base, members, variants)
        # TODO Worth changing the visitor signature, so we could
        # directly use rather than repeat type.is_implicit()?
        if not name.startswith('q_'):
            # only explicit types need an allocating visit
            self.decl += gen_visit_decl(name)
            self.defn += gen_visit_object(name, base, members, variants)

    def visit_alternate_type(self, name, info, variants):
        self.decl += gen_visit_decl(name)
        self.defn += gen_visit_alternate(name, variants)

# If you link code generated from multiple schemata, you want only one
# instance of the code for built-in types.  Generate it only when
# do_builtins, enabled by command line option -b.  See also
# QAPISchemaGenVisitVisitor.visit_end().
do_builtins = False

(input_file, output_dir, do_c, do_h, prefix, opts) = \
    parse_command_line("b", ["builtins"])

for o, a in opts:
    if o in ("-b", "--builtins"):
        do_builtins = True

c_comment = '''
/*
 * schema-defined QAPI visitor functions
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
 * schema-defined QAPI visitor functions
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
                            'qapi-visit.c', 'qapi-visit.h',
                            c_comment, h_comment)

fdef.write(mcgen('''
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "%(prefix)sqapi-visit.h"
''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "qapi/visitor.h"
#include "qapi/qmp/qerror.h"
#include "%(prefix)sqapi-types.h"

''',
                  prefix=prefix))

schema = QAPISchema(input_file)
gen = QAPISchemaGenVisitVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
