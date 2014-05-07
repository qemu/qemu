#
# QAPI visitor generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Michael Roth    <mdroth@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *
import sys
import os
import getopt
import errno

def generate_visit_struct_fields(name, field_prefix, fn_prefix, members, base = None):
    substructs = []
    ret = ''
    if not fn_prefix:
        full_name = name
    else:
        full_name = "%s_%s" % (name, fn_prefix)

    for argname, argentry, optional, structured in parse_args(members):
        if structured:
            if not fn_prefix:
                nested_fn_prefix = argname
            else:
                nested_fn_prefix = "%s_%s" % (fn_prefix, argname)

            nested_field_prefix = "%s%s." % (field_prefix, argname)
            ret += generate_visit_struct_fields(name, nested_field_prefix,
                                                nested_fn_prefix, argentry)
            ret += mcgen('''

static void visit_type_%(full_name)s_field_%(c_name)s(Visitor *m, %(name)s **obj, Error **errp)
{
    Error *err = NULL;
''',
                         name=name, full_name=full_name, c_name=c_var(argname))
            push_indent()
            ret += generate_visit_struct_body(full_name, argname, argentry)
            pop_indent()
            ret += mcgen('''
}
''')

    ret += mcgen('''

static void visit_type_%(full_name)s_fields(Visitor *m, %(name)s ** obj, Error **errp)
{
    Error *err = NULL;
''',
        name=name, full_name=full_name)
    push_indent()

    if base:
        ret += mcgen('''
visit_start_implicit_struct(m, (void**) &(*obj)->%(c_name)s, sizeof(%(type)s), &err);
if (!err) {
    visit_type_%(type)s_fields(m, &(*obj)->%(c_prefix)s%(c_name)s, &err);
    error_propagate(errp, err);
    err = NULL;
    visit_end_implicit_struct(m, &err);
}
''',
                     c_prefix=c_var(field_prefix),
                     type=type_name(base), c_name=c_var('base'))

    for argname, argentry, optional, structured in parse_args(members):
        if optional:
            ret += mcgen('''
visit_optional(m, &(*obj)->%(c_prefix)shas_%(c_name)s, "%(name)s", &err);
if ((*obj)->%(prefix)shas_%(c_name)s) {
''',
                         c_prefix=c_var(field_prefix), prefix=field_prefix,
                         c_name=c_var(argname), name=argname)
            push_indent()

        if structured:
            ret += mcgen('''
visit_type_%(full_name)s_field_%(c_name)s(m, obj, &err);
''',
                         full_name=full_name, c_name=c_var(argname))
        else:
            ret += mcgen('''
visit_type_%(type)s(m, &(*obj)->%(c_prefix)s%(c_name)s, "%(name)s", &err);
''',
                         c_prefix=c_var(field_prefix), prefix=field_prefix,
                         type=type_name(argentry), c_name=c_var(argname),
                         name=argname)

        if optional:
            pop_indent()
            ret += mcgen('''
}
''')

    pop_indent()
    ret += mcgen('''

    error_propagate(errp, err);
}
''')
    return ret


def generate_visit_struct_body(field_prefix, name, members):
    ret = mcgen('''
if (!error_is_set(errp)) {
''')
    push_indent()

    if not field_prefix:
        full_name = name
    else:
        full_name = "%s_%s" % (field_prefix, name)

    if len(field_prefix):
        ret += mcgen('''
visit_start_struct(m, NULL, "", "%(name)s", 0, &err);
''',
                name=name)
    else:
        ret += mcgen('''
Error *err = NULL;
visit_start_struct(m, (void **)obj, "%(name)s", name, sizeof(%(name)s), &err);
''',
                name=name)

    ret += mcgen('''
if (!err) {
    if (*obj) {
        visit_type_%(name)s_fields(m, obj, &err);
        error_propagate(errp, err);
        err = NULL;
    }
''',
        name=full_name)

    ret += mcgen('''
    /* Always call end_struct if start_struct succeeded.  */
    visit_end_struct(m, &err);
}
error_propagate(errp, err);
''')
    pop_indent()
    ret += mcgen('''
}
''')
    return ret

def generate_visit_struct(expr):

    name = expr['type']
    members = expr['data']
    base = expr.get('base')

    ret = generate_visit_struct_fields(name, "", "", members, base)

    ret += mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s ** obj, const char *name, Error **errp)
{
''',
                name=name)

    push_indent()
    ret += generate_visit_struct_body("", name, members)
    pop_indent()

    ret += mcgen('''
}
''')
    return ret

def generate_visit_list(name, members):
    return mcgen('''

void visit_type_%(name)sList(Visitor *m, %(name)sList ** obj, const char *name, Error **errp)
{
    GenericList *i, **prev = (GenericList **)obj;
    Error *err = NULL;

    if (!error_is_set(errp)) {
        visit_start_list(m, name, &err);
        if (!err) {
            for (; (i = visit_next_list(m, prev, &err)) != NULL; prev = &i) {
                %(name)sList *native_i = (%(name)sList *)i;
                visit_type_%(name)s(m, &native_i->value, NULL, &err);
            }
            error_propagate(errp, err);
            err = NULL;

            /* Always call end_list if start_list succeeded.  */
            visit_end_list(m, &err);
        }
        error_propagate(errp, err);
    }
}
''',
                name=name)

def generate_visit_enum(name, members):
    return mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s * obj, const char *name, Error **errp)
{
    visit_type_enum(m, (int *)obj, %(name)s_lookup, "%(name)s", name, errp);
}
''',
                 name=name)

def generate_visit_anon_union(name, members):
    ret = mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s ** obj, const char *name, Error **errp)
{
    Error *err = NULL;

    if (!error_is_set(errp)) {
        visit_start_implicit_struct(m, (void**) obj, sizeof(%(name)s), &err);
        visit_get_next_type(m, (int*) &(*obj)->kind, %(name)s_qtypes, name, &err);
        switch ((*obj)->kind) {
''',
    name=name)

    # For anon union, always use the default enum type automatically generated
    # as "'%sKind' % (name)"
    disc_type = '%sKind' % (name)

    for key in members:
        assert (members[key] in builtin_types
            or find_struct(members[key])
            or find_union(members[key])), "Invalid anonymous union member"

        enum_full_value = generate_enum_full_value(disc_type, key)
        ret += mcgen('''
        case %(enum_full_value)s:
            visit_type_%(c_type)s(m, &(*obj)->%(c_name)s, name, &err);
            break;
''',
                enum_full_value = enum_full_value,
                c_type = type_name(members[key]),
                c_name = c_fun(key))

    ret += mcgen('''
        default:
            abort();
        }
        error_propagate(errp, err);
        err = NULL;
        visit_end_implicit_struct(m, &err);
    }
}
''')

    return ret


def generate_visit_union(expr):

    name = expr['union']
    members = expr['data']

    base = expr.get('base')
    discriminator = expr.get('discriminator')

    if discriminator == {}:
        assert not base
        return generate_visit_anon_union(name, members)

    enum_define = discriminator_find_enum_define(expr)
    if enum_define:
        # Use the enum type as discriminator
        ret = ""
        disc_type = enum_define['enum_name']
    else:
        # There will always be a discriminator in the C switch code, by default it
        # is an enum type generated silently as "'%sKind' % (name)"
        ret = generate_visit_enum('%sKind' % name, members.keys())
        disc_type = '%sKind' % (name)

    if base:
        base_fields = find_struct(base)['data']
        if discriminator:
            base_fields = base_fields.copy()
            del base_fields[discriminator]
        ret += generate_visit_struct_fields(name, "", "", base_fields)

    ret += mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s ** obj, const char *name, Error **errp)
{
    Error *err = NULL;

    if (!error_is_set(errp)) {
        visit_start_struct(m, (void **)obj, "%(name)s", name, sizeof(%(name)s), &err);
        if (!err) {
            if (*obj) {
''',
                 name=name)

    push_indent()
    push_indent()

    if base:
        ret += mcgen('''
        visit_type_%(name)s_fields(m, obj, &err);
''',
            name=name)

    if not discriminator:
        disc_key = "type"
    else:
        disc_key = discriminator
    ret += mcgen('''
        visit_type_%(disc_type)s(m, &(*obj)->kind, "%(disc_key)s", &err);
        if (!err) {
            switch ((*obj)->kind) {
''',
                 disc_type = disc_type,
                 disc_key = disc_key)

    for key in members:
        if not discriminator:
            fmt = 'visit_type_%(c_type)s(m, &(*obj)->%(c_name)s, "data", &err);'
        else:
            fmt = '''visit_start_implicit_struct(m, (void**) &(*obj)->%(c_name)s, sizeof(%(c_type)s), &err);
                if (!err) {
                    visit_type_%(c_type)s_fields(m, &(*obj)->%(c_name)s, &err);
                    error_propagate(errp, err);
                    err = NULL;
                    visit_end_implicit_struct(m, &err);
                }'''

        enum_full_value = generate_enum_full_value(disc_type, key)
        ret += mcgen('''
            case %(enum_full_value)s:
                ''' + fmt + '''
                break;
''',
                enum_full_value = enum_full_value,
                c_type=type_name(members[key]),
                c_name=c_fun(key))

    ret += mcgen('''
            default:
                abort();
            }
        }
        error_propagate(errp, err);
        err = NULL;
''')
    pop_indent()
    pop_indent()

    ret += mcgen('''
            }
            /* Always call end_struct if start_struct succeeded.  */
            visit_end_struct(m, &err);
        }
        error_propagate(errp, err);
    }
}
''')

    return ret

def generate_declaration(name, members, genlist=True, builtin_type=False):
    ret = ""
    if not builtin_type:
        ret += mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s ** obj, const char *name, Error **errp);
''',
                    name=name)

    if genlist:
        ret += mcgen('''
void visit_type_%(name)sList(Visitor *m, %(name)sList ** obj, const char *name, Error **errp);
''',
                 name=name)

    return ret

def generate_enum_declaration(name, members, genlist=True):
    ret = ""
    if genlist:
        ret += mcgen('''
void visit_type_%(name)sList(Visitor *m, %(name)sList ** obj, const char *name, Error **errp);
''',
                     name=name)

    return ret

def generate_decl_enum(name, members, genlist=True):
    return mcgen('''

void visit_type_%(name)s(Visitor *m, %(name)s * obj, const char *name, Error **errp);
''',
                name=name)

try:
    opts, args = getopt.gnu_getopt(sys.argv[1:], "chbp:i:o:",
                                   ["source", "header", "builtins", "prefix=",
                                    "input-file=", "output-dir="])
except getopt.GetoptError, err:
    print str(err)
    sys.exit(1)

input_file = ""
output_dir = ""
prefix = ""
c_file = 'qapi-visit.c'
h_file = 'qapi-visit.h'

do_c = False
do_h = False
do_builtins = False

for o, a in opts:
    if o in ("-p", "--prefix"):
        prefix = a
    elif o in ("-i", "--input-file"):
        input_file = a
    elif o in ("-o", "--output-dir"):
        output_dir = a + "/"
    elif o in ("-c", "--source"):
        do_c = True
    elif o in ("-h", "--header"):
        do_h = True
    elif o in ("-b", "--builtins"):
        do_builtins = True

if not do_c and not do_h:
    do_c = True
    do_h = True

c_file = output_dir + prefix + c_file
h_file = output_dir + prefix + h_file

try:
    os.makedirs(output_dir)
except os.error, e:
    if e.errno != errno.EEXIST:
        raise

def maybe_open(really, name, opt):
    if really:
        return open(name, opt)
    else:
        import StringIO
        return StringIO.StringIO()

fdef = maybe_open(do_c, c_file, 'w')
fdecl = maybe_open(do_h, h_file, 'w')

fdef.write(mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT MODIFY */

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

#include "qemu-common.h"
#include "%(header)s"
''',
                 header=basename(h_file)))

fdecl.write(mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI visitor function
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

#ifndef %(guard)s
#define %(guard)s

#include "qapi/visitor.h"
#include "%(prefix)sqapi-types.h"

''',
                  prefix=prefix, guard=guardname(h_file)))

exprs = parse_schema(input_file)

# to avoid header dependency hell, we always generate declarations
# for built-in types in our header files and simply guard them
fdecl.write(guardstart("QAPI_VISIT_BUILTIN_VISITOR_DECL"))
for typename in builtin_types:
    fdecl.write(generate_declaration(typename, None, genlist=True,
                                     builtin_type=True))
fdecl.write(guardend("QAPI_VISIT_BUILTIN_VISITOR_DECL"))

# ...this doesn't work for cases where we link in multiple objects that
# have the functions defined, so we use -b option to provide control
# over these cases
if do_builtins:
    for typename in builtin_types:
        fdef.write(generate_visit_list(typename, None))

for expr in exprs:
    if expr.has_key('type'):
        ret = generate_visit_struct(expr)
        ret += generate_visit_list(expr['type'], expr['data'])
        fdef.write(ret)

        ret = generate_declaration(expr['type'], expr['data'])
        fdecl.write(ret)
    elif expr.has_key('union'):
        ret = generate_visit_union(expr)
        ret += generate_visit_list(expr['union'], expr['data'])
        fdef.write(ret)

        enum_define = discriminator_find_enum_define(expr)
        ret = ""
        if not enum_define:
            ret = generate_decl_enum('%sKind' % expr['union'],
                                     expr['data'].keys())
        ret += generate_declaration(expr['union'], expr['data'])
        fdecl.write(ret)
    elif expr.has_key('enum'):
        ret = generate_visit_list(expr['enum'], expr['data'])
        ret += generate_visit_enum(expr['enum'], expr['data'])
        fdef.write(ret)

        ret = generate_decl_enum(expr['enum'], expr['data'])
        ret += generate_enum_declaration(expr['enum'], expr['data'])
        fdecl.write(ret)

fdecl.write('''
#endif
''')

fdecl.flush()
fdecl.close()

fdef.flush()
fdef.close()
