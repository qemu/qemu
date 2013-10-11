#
# QAPI visitor generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Michael Roth    <mdroth@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *
import sys
import os
import getopt
import errno

def generate_visit_struct_fields(name, field_prefix, fn_prefix, members, base = None):
    substructs = []
    ret = ''
    full_name = name if not fn_prefix else "%s_%s" % (name, fn_prefix)

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

static void visit_type_%(full_name)s_fields(Visitor *m, %(name)s ** obj, Error **errp)
{
    Error *err = NULL;
''',
        name=name, full_name=full_name)
    push_indent()

    if base:
        ret += mcgen('''
visit_start_implicit_struct(m, obj ? (void**) &(*obj)->%(c_name)s : NULL, sizeof(%(type)s), &err);
if (!err) {
    visit_type_%(type)s_fields(m, obj ? &(*obj)->%(c_prefix)s%(c_name)s : NULL, &err);
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
visit_start_optional(m, obj ? &(*obj)->%(c_prefix)shas_%(c_name)s : NULL, "%(name)s", &err);
if (obj && (*obj)->%(prefix)shas_%(c_name)s) {
''',
                         c_prefix=c_var(field_prefix), prefix=field_prefix,
                         c_name=c_var(argname), name=argname)
            push_indent()

        if structured:
            ret += generate_visit_struct_body(full_name, argname, argentry)
        else:
            ret += mcgen('''
visit_type_%(type)s(m, obj ? &(*obj)->%(c_prefix)s%(c_name)s : NULL, "%(name)s", &err);
''',
                         c_prefix=c_var(field_prefix), prefix=field_prefix,
                         type=type_name(argentry), c_name=c_var(argname),
                         name=argname)

        if optional:
            pop_indent()
            ret += mcgen('''
}
visit_end_optional(m, &err);
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

    full_name = name if not field_prefix else "%s_%s" % (field_prefix, name)

    if len(field_prefix):
        ret += mcgen('''
Error **errp = &err; /* from outer scope */
Error *err = NULL;
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
    if (!obj || *obj) {
        visit_type_%(name)s_fields(m, obj, &err);
        error_propagate(errp, err);
        err = NULL;
    }
''',
        name=full_name)

    pop_indent()
    ret += mcgen('''
        /* Always call end_struct if start_struct succeeded.  */
        visit_end_struct(m, &err);
    }
    error_propagate(errp, err);
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

    for key in members:
        assert (members[key] in builtin_types
            or find_struct(members[key])
            or find_union(members[key])), "Invalid anonymous union member"

        ret += mcgen('''
        case %(abbrev)s_KIND_%(enum)s:
            visit_type_%(c_type)s(m, &(*obj)->%(c_name)s, name, &err);
            break;
''',
                abbrev = de_camel_case(name).upper(),
                enum = c_fun(de_camel_case(key),False).upper(),
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

    ret = generate_visit_enum('%sKind' % name, members.keys())

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
            if (obj && *obj) {
''',
                 name=name)


    push_indent()
    push_indent()
    push_indent()

    if base:
        ret += mcgen('''
    visit_type_%(name)s_fields(m, obj, &err);
''',
            name=name)

    pop_indent()
    ret += mcgen('''
        visit_type_%(name)sKind(m, &(*obj)->kind, "%(type)s", &err);
        if (!err) {
            switch ((*obj)->kind) {
''',
                 name=name, type="type" if not discriminator else discriminator)

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

        ret += mcgen('''
            case %(abbrev)s_KIND_%(enum)s:
                ''' + fmt + '''
                break;
''',
                abbrev = de_camel_case(name).upper(),
                enum = c_fun(de_camel_case(key),False).upper(),
                c_type=type_name(members[key]),
                c_name=c_fun(key))

    ret += mcgen('''
            default:
                abort();
            }
        }
        error_propagate(errp, err);
        err = NULL;
    }
''')
    pop_indent()
    ret += mcgen('''
        /* Always call end_struct if start_struct succeeded.  */
        visit_end_struct(m, &err);
    }
    error_propagate(errp, err);
}
''')

    pop_indent();
    ret += mcgen('''
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
    opts, args = getopt.gnu_getopt(sys.argv[1:], "chbp:o:",
                                   ["source", "header", "builtins", "prefix=",
                                    "output-dir="])
except getopt.GetoptError, err:
    print str(err)
    sys.exit(1)

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

exprs = parse_schema(sys.stdin)

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
    fdef.write(guardstart("QAPI_VISIT_BUILTIN_VISITOR_DEF"))
    for typename in builtin_types:
        fdef.write(generate_visit_list(typename, None))
    fdef.write(guardend("QAPI_VISIT_BUILTIN_VISITOR_DEF"))

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

        ret = generate_decl_enum('%sKind' % expr['union'], expr['data'].keys())
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
