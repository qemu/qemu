#
# QAPI visitor generator
#
# Copyright IBM, Corp. 2011
# Copyright (C) 2014-2015 Red Hat, Inc.
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

# visit_type_FOO_implicit() is emitted as needed; track if it has already
# been output.
implicit_structs_seen = set()

# visit_type_FOO_fields() is always emitted; track if a forward declaration
# or implementation has already been output.
struct_fields_seen = set()


def gen_visit_decl(name, scalar=False):
    c_type = c_name(name) + ' *'
    if not scalar:
        c_type += '*'
    return mcgen('''
void visit_type_%(c_name)s(Visitor *v, %(c_type)sobj, const char *name, Error **errp);
''',
                 c_name=c_name(name), c_type=c_type)


def gen_visit_fields_decl(typ):
    ret = ''
    if typ.name not in struct_fields_seen:
        ret += mcgen('''

static void visit_type_%(c_type)s_fields(Visitor *v, %(c_type)s **obj, Error **errp);
''',
                     c_type=typ.c_name())
        struct_fields_seen.add(typ.name)
    return ret


def gen_visit_implicit_struct(typ):
    if typ in implicit_structs_seen:
        return ''
    implicit_structs_seen.add(typ)

    ret = gen_visit_fields_decl(typ)

    ret += mcgen('''

static void visit_type_implicit_%(c_type)s(Visitor *v, %(c_type)s **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_implicit_struct(v, (void **)obj, sizeof(%(c_type)s), &err);
    if (!err) {
        visit_type_%(c_type)s_fields(v, obj, errp);
        visit_end_implicit_struct(v, &err);
    }
    error_propagate(errp, err);
}
''',
                 c_type=typ.c_name())
    return ret


def gen_visit_struct_fields(name, base, members):
    ret = ''

    if base:
        ret += gen_visit_fields_decl(base)

    struct_fields_seen.add(name)
    ret += mcgen('''

static void visit_type_%(c_name)s_fields(Visitor *v, %(c_name)s **obj, Error **errp)
{
    Error *err = NULL;

''',
                 c_name=c_name(name))

    if base:
        ret += mcgen('''
    visit_type_%(c_type)s_fields(v, (%(c_type)s **)obj, &err);
''',
                     c_type=base.c_name())
        ret += gen_err_check()

    ret += gen_visit_fields(members, prefix='(*obj)->')

    # 'goto out' produced for base, and by gen_visit_fields() for each member
    if base or members:
        ret += mcgen('''

out:
''')
    ret += mcgen('''
    error_propagate(errp, err);
}
''')
    return ret


def gen_visit_struct(name, base, members):
    ret = gen_visit_struct_fields(name, base, members)

    # FIXME: if *obj is NULL on entry, and visit_start_struct() assigns to
    # *obj, but then visit_type_FOO_fields() fails, we should clean up *obj
    # rather than leaving it non-NULL. As currently written, the caller must
    # call qapi_free_FOO() to avoid a memory leak of the partial FOO.
    ret += mcgen('''

void visit_type_%(c_name)s(Visitor *v, %(c_name)s **obj, const char *name, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, (void **)obj, "%(name)s", name, sizeof(%(c_name)s), &err);
    if (!err) {
        if (*obj) {
            visit_type_%(c_name)s_fields(v, obj, errp);
        }
        visit_end_struct(v, &err);
    }
    error_propagate(errp, err);
}
''',
                 name=name, c_name=c_name(name))

    return ret


def gen_visit_list(name, element_type):
    # FIXME: if *obj is NULL on entry, and the first visit_next_list()
    # assigns to *obj, while a later one fails, we should clean up *obj
    # rather than leaving it non-NULL. As currently written, the caller must
    # call qapi_free_FOOList() to avoid a memory leak of the partial FOOList.
    return mcgen('''

void visit_type_%(c_name)s(Visitor *v, %(c_name)s **obj, const char *name, Error **errp)
{
    Error *err = NULL;
    GenericList *i, **prev;

    visit_start_list(v, name, &err);
    if (err) {
        goto out;
    }

    for (prev = (GenericList **)obj;
         !err && (i = visit_next_list(v, prev, &err)) != NULL;
         prev = &i) {
        %(c_name)s *native_i = (%(c_name)s *)i;
        visit_type_%(c_elt_type)s(v, &native_i->value, NULL, &err);
    }

    error_propagate(errp, err);
    err = NULL;
    visit_end_list(v, &err);
out:
    error_propagate(errp, err);
}
''',
                 c_name=c_name(name), c_elt_type=element_type.c_name())


def gen_visit_enum(name):
    return mcgen('''

void visit_type_%(c_name)s(Visitor *v, %(c_name)s *obj, const char *name, Error **errp)
{
    visit_type_enum(v, (int *)obj, %(c_name)s_lookup, "%(name)s", name, errp);
}
''',
                 c_name=c_name(name), name=name)


def gen_visit_alternate(name, variants):
    ret = mcgen('''

void visit_type_%(c_name)s(Visitor *v, %(c_name)s **obj, const char *name, Error **errp)
{
    Error *err = NULL;

    visit_start_implicit_struct(v, (void**) obj, sizeof(%(c_name)s), &err);
    if (err) {
        goto out;
    }
    visit_get_next_type(v, (int*) &(*obj)->type, %(c_name)s_qtypes, name, &err);
    if (err) {
        goto out_obj;
    }
    switch ((*obj)->type) {
''',
                c_name=c_name(name))

    for var in variants.variants:
        ret += mcgen('''
    case %(case)s:
        visit_type_%(c_type)s(v, &(*obj)->u.%(c_name)s, name, &err);
        break;
''',
                     case=c_enum_const(variants.tag_member.type.name,
                                       var.name),
                     c_type=var.type.c_name(),
                     c_name=c_name(var.name))

    ret += mcgen('''
    default:
        abort();
    }
out_obj:
    error_propagate(errp, err);
    err = NULL;
    visit_end_implicit_struct(v, &err);
out:
    error_propagate(errp, err);
}
''')

    return ret


def gen_visit_union(name, base, variants):
    ret = ''

    if base:
        ret += gen_visit_fields_decl(base)

    for var in variants.variants:
        # Ugly special case for simple union TODO get rid of it
        if not var.simple_union_type():
            ret += gen_visit_implicit_struct(var.type)

    ret += mcgen('''

void visit_type_%(c_name)s(Visitor *v, %(c_name)s **obj, const char *name, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, (void **)obj, "%(name)s", name, sizeof(%(c_name)s), &err);
    if (err) {
        goto out;
    }
    if (!*obj) {
        goto out_obj;
    }
''',
                 c_name=c_name(name), name=name)

    if base:
        ret += mcgen('''
    visit_type_%(c_name)s_fields(v, (%(c_name)s **)obj, &err);
''',
                     c_name=base.c_name())
    else:
        ret += mcgen('''
    visit_type_%(c_type)s(v, &(*obj)->%(c_name)s, "%(name)s", &err);
''',
                     c_type=variants.tag_member.type.c_name(),
                     c_name=c_name(variants.tag_member.name),
                     name=variants.tag_member.name)
    ret += gen_err_check(label='out_obj')
    ret += mcgen('''
    if (!visit_start_union(v, !!(*obj)->u.data, &err) || err) {
        goto out_obj;
    }
    switch ((*obj)->%(c_name)s) {
''',
                 c_name=c_name(variants.tag_member.name))

    for var in variants.variants:
        # TODO ugly special case for simple union
        simple_union_type = var.simple_union_type()
        ret += mcgen('''
    case %(case)s:
''',
                     case=c_enum_const(variants.tag_member.type.name,
                                       var.name))
        if simple_union_type:
            ret += mcgen('''
        visit_type_%(c_type)s(v, &(*obj)->u.%(c_name)s, "data", &err);
''',
                         c_type=simple_union_type.c_name(),
                         c_name=c_name(var.name))
        else:
            ret += mcgen('''
        visit_type_implicit_%(c_type)s(v, &(*obj)->u.%(c_name)s, &err);
''',
                         c_type=var.type.c_name(),
                         c_name=c_name(var.name))
        ret += mcgen('''
        break;
''')

    ret += mcgen('''
    default:
        abort();
    }
out_obj:
    error_propagate(errp, err);
    err = NULL;
    if (*obj) {
        visit_end_union(v, !!(*obj)->u.data, &err);
    }
    error_propagate(errp, err);
    err = NULL;
    visit_end_struct(v, &err);
out:
    error_propagate(errp, err);
}
''')

    return ret


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

    def visit_needed(self, entity):
        # Visit everything except implicit objects
        return not (entity.is_implicit() and
                    isinstance(entity, QAPISchemaObjectType))

    def visit_enum_type(self, name, info, values, prefix):
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
        self.decl += gen_visit_decl(name)
        if variants:
            assert not members      # not implemented
            self.defn += gen_visit_union(name, base, variants)
        else:
            self.defn += gen_visit_struct(name, base, members)

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
#include "qemu-common.h"
#include "%(prefix)sqapi-visit.h"
''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "qapi/visitor.h"
#include "%(prefix)sqapi-types.h"

''',
                  prefix=prefix))

schema = QAPISchema(input_file)
gen = QAPISchemaGenVisitVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
