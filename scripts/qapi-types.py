#
# QAPI types generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *
import sys
import os
import getopt
import errno

def generate_fwd_struct(name, members, builtin_type=False):
    if builtin_type:
        return mcgen('''

typedef struct %(name)sList
{
    union {
        %(type)s value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                     type=c_type(name),
                     name=name)

    return mcgen('''

typedef struct %(name)s %(name)s;

typedef struct %(name)sList
{
    union {
        %(name)s *value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=name)

def generate_fwd_enum_struct(name, members):
    return mcgen('''
typedef struct %(name)sList
{
    union {
        %(name)s value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=name)

def generate_struct_fields(members):
    ret = ''

    for argname, argentry, optional, structured in parse_args(members):
        if optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_var(argname))
        if structured:
            push_indent()
            ret += generate_struct({ "field": argname, "data": argentry})
            pop_indent()
        else:
            ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=c_type(argentry), c_name=c_var(argname))

    return ret

def generate_struct(expr):

    structname = expr.get('type', "")
    fieldname = expr.get('field', "")
    members = expr['data']
    base = expr.get('base')

    ret = mcgen('''
struct %(name)s
{
''',
          name=structname)

    if base:
        ret += generate_struct_fields({'base': base})

    ret += generate_struct_fields(members)

    if len(fieldname):
        fieldname = " " + fieldname
    ret += mcgen('''
}%(field)s;
''',
            field=fieldname)

    return ret

def generate_enum_lookup(name, values):
    ret = mcgen('''
const char *%(name)s_lookup[] = {
''',
                         name=name)
    i = 0
    for value in values:
        ret += mcgen('''
    "%(value)s",
''',
                     value=value)

    ret += mcgen('''
    NULL,
};

''')
    return ret

def generate_enum(name, values):
    lookup_decl = mcgen('''
extern const char *%(name)s_lookup[];
''',
                name=name)

    enum_decl = mcgen('''
typedef enum %(name)s
{
''',
                name=name)

    # append automatically generated _MAX value
    enum_values = values + [ 'MAX' ]

    i = 0
    for value in enum_values:
        enum_full_value = generate_enum_full_value(name, value)
        enum_decl += mcgen('''
    %(enum_full_value)s = %(i)d,
''',
                     enum_full_value = enum_full_value,
                     i=i)
        i += 1

    enum_decl += mcgen('''
} %(name)s;
''',
                 name=name)

    return lookup_decl + enum_decl

def generate_anon_union_qtypes(expr):

    name = expr['union']
    members = expr['data']

    ret = mcgen('''
const int %(name)s_qtypes[QTYPE_MAX] = {
''',
    name=name)

    for key in members:
        qapi_type = members[key]
        if builtin_type_qtypes.has_key(qapi_type):
            qtype = builtin_type_qtypes[qapi_type]
        elif find_struct(qapi_type):
            qtype = "QTYPE_QDICT"
        elif find_union(qapi_type):
            qtype = "QTYPE_QDICT"
        else:
            assert False, "Invalid anonymous union member"

        ret += mcgen('''
    [ %(qtype)s ] = %(abbrev)s_KIND_%(enum)s,
''',
        qtype = qtype,
        abbrev = de_camel_case(name).upper(),
        enum = c_fun(de_camel_case(key),False).upper())

    ret += mcgen('''
};
''')
    return ret


def generate_union(expr):

    name = expr['union']
    typeinfo = expr['data']

    base = expr.get('base')
    discriminator = expr.get('discriminator')

    enum_define = discriminator_find_enum_define(expr)
    if enum_define:
        discriminator_type_name = enum_define['enum_name']
    else:
        discriminator_type_name = '%sKind' % (name)

    ret = mcgen('''
struct %(name)s
{
    %(discriminator_type_name)s kind;
    union {
        void *data;
''',
                name=name,
                discriminator_type_name=discriminator_type_name)

    for key in typeinfo:
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=c_type(typeinfo[key]),
                     c_name=c_fun(key))

    ret += mcgen('''
    };
''')

    if base:
        base_fields = find_struct(base)['data']
        if discriminator:
            base_fields = base_fields.copy()
            del base_fields[discriminator]
        ret += generate_struct_fields(base_fields)
    else:
        assert not discriminator

    ret += mcgen('''
};
''')
    if discriminator == {}:
        ret += mcgen('''
extern const int %(name)s_qtypes[];
''',
            name=name)


    return ret

def generate_type_cleanup_decl(name):
    ret = mcgen('''
void qapi_free_%(type)s(%(c_type)s obj);
''',
                c_type=c_type(name),type=name)
    return ret

def generate_type_cleanup(name):
    ret = mcgen('''

void qapi_free_%(type)s(%(c_type)s obj)
{
    QapiDeallocVisitor *md;
    Visitor *v;

    if (!obj) {
        return;
    }

    md = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(md);
    visit_type_%(type)s(v, &obj, NULL, NULL);
    qapi_dealloc_visitor_cleanup(md);
}
''',
                c_type=c_type(name),type=name)
    return ret


try:
    opts, args = getopt.gnu_getopt(sys.argv[1:], "chbp:i:o:",
                                   ["source", "header", "builtins",
                                    "prefix=", "input-file=", "output-dir="])
except getopt.GetoptError, err:
    print str(err)
    sys.exit(1)

output_dir = ""
input_file = ""
prefix = ""
c_file = 'qapi-types.c'
h_file = 'qapi-types.h'

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
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * deallocation functions for schema-defined QAPI types
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"

''',             prefix=prefix))

fdecl.write(mcgen('''
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI types
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

#include <stdbool.h>
#include <stdint.h>

''',
                  guard=guardname(h_file)))

exprs = parse_schema(input_file)
exprs = filter(lambda expr: not expr.has_key('gen'), exprs)

fdecl.write(guardstart("QAPI_TYPES_BUILTIN_STRUCT_DECL"))
for typename in builtin_types:
    fdecl.write(generate_fwd_struct(typename, None, builtin_type=True))
fdecl.write(guardend("QAPI_TYPES_BUILTIN_STRUCT_DECL"))

for expr in exprs:
    ret = "\n"
    if expr.has_key('type'):
        ret += generate_fwd_struct(expr['type'], expr['data'])
    elif expr.has_key('enum'):
        ret += generate_enum(expr['enum'], expr['data']) + "\n"
        ret += generate_fwd_enum_struct(expr['enum'], expr['data'])
        fdef.write(generate_enum_lookup(expr['enum'], expr['data']))
    elif expr.has_key('union'):
        ret += generate_fwd_struct(expr['union'], expr['data']) + "\n"
        enum_define = discriminator_find_enum_define(expr)
        if not enum_define:
            ret += generate_enum('%sKind' % expr['union'], expr['data'].keys())
            fdef.write(generate_enum_lookup('%sKind' % expr['union'],
                                            expr['data'].keys()))
        if expr.get('discriminator') == {}:
            fdef.write(generate_anon_union_qtypes(expr))
    else:
        continue
    fdecl.write(ret)

# to avoid header dependency hell, we always generate declarations
# for built-in types in our header files and simply guard them
fdecl.write(guardstart("QAPI_TYPES_BUILTIN_CLEANUP_DECL"))
for typename in builtin_types:
    fdecl.write(generate_type_cleanup_decl(typename + "List"))
fdecl.write(guardend("QAPI_TYPES_BUILTIN_CLEANUP_DECL"))

# ...this doesn't work for cases where we link in multiple objects that
# have the functions defined, so we use -b option to provide control
# over these cases
if do_builtins:
    fdef.write(guardstart("QAPI_TYPES_BUILTIN_CLEANUP_DEF"))
    for typename in builtin_types:
        fdef.write(generate_type_cleanup(typename + "List"))
    fdef.write(guardend("QAPI_TYPES_BUILTIN_CLEANUP_DEF"))

for expr in exprs:
    ret = "\n"
    if expr.has_key('type'):
        ret += generate_struct(expr) + "\n"
        ret += generate_type_cleanup_decl(expr['type'] + "List")
        fdef.write(generate_type_cleanup(expr['type'] + "List") + "\n")
        ret += generate_type_cleanup_decl(expr['type'])
        fdef.write(generate_type_cleanup(expr['type']) + "\n")
    elif expr.has_key('union'):
        ret += generate_union(expr)
        ret += generate_type_cleanup_decl(expr['union'] + "List")
        fdef.write(generate_type_cleanup(expr['union'] + "List") + "\n")
        ret += generate_type_cleanup_decl(expr['union'])
        fdef.write(generate_type_cleanup(expr['union']) + "\n")
    elif expr.has_key('enum'):
        ret += generate_type_cleanup_decl(expr['enum'] + "List")
        fdef.write(generate_type_cleanup(expr['enum'] + "List") + "\n")
    else:
        continue
    fdecl.write(ret)

fdecl.write('''
#endif
''')

fdecl.flush()
fdecl.close()

fdef.flush()
fdef.close()
