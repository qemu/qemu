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

def generate_fwd_builtin(name):
    return mcgen('''

typedef struct %(name)sList {
    union {
        %(type)s value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                 type=c_type(name),
                 name=name)

def generate_fwd_struct(name):
    return mcgen('''

typedef struct %(name)s %(name)s;

typedef struct %(name)sList {
    union {
        %(name)s *value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=c_name(name))

def generate_fwd_enum_struct(name):
    return mcgen('''

typedef struct %(name)sList {
    union {
        %(name)s value;
        uint64_t padding;
    };
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=c_name(name))

def generate_struct_fields(members):
    ret = ''

    for argname, argentry, optional in parse_args(members):
        if optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_name(argname))
        ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=c_type(argentry), c_name=c_name(argname))

    return ret

def generate_struct(expr):

    structname = expr.get('struct', "")
    members = expr['data']
    base = expr.get('base')

    ret = mcgen('''

struct %(name)s {
''',
          name=c_name(structname))

    if base:
        ret += generate_struct_fields({'base': base})

    ret += generate_struct_fields(members)

    # Make sure that all structs have at least one field; this avoids
    # potential issues with attempting to malloc space for zero-length structs
    # in C, and also incompatibility with C++ (where an empty struct is size 1).
    if not base and not members:
            ret += mcgen('''
    char qapi_dummy_field_for_empty_struct;
''')

    ret += mcgen('''
};
''')

    return ret

def generate_enum_lookup(name, values, prefix=None):
    ret = mcgen('''

const char *const %(name)s_lookup[] = {
''',
                name=c_name(name))
    for value in values:
        index = c_enum_const(name, value, prefix)
        ret += mcgen('''
    [%(index)s] = "%(value)s",
''',
                     index = index, value = value)

    max_index = c_enum_const(name, 'MAX', prefix)
    ret += mcgen('''
    [%(max_index)s] = NULL,
};
''',
        max_index=max_index)
    return ret

def generate_enum(name, values, prefix=None):
    name = c_name(name)
    lookup_decl = mcgen('''

extern const char *const %(name)s_lookup[];
''',
                name=name)

    enum_decl = mcgen('''

typedef enum %(name)s {
''',
                name=name)

    # append automatically generated _MAX value
    enum_values = values + [ 'MAX' ]

    i = 0
    for value in enum_values:
        enum_full_value = c_enum_const(name, value, prefix)
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

    return enum_decl + lookup_decl

def generate_alternate_qtypes(expr):

    name = expr['alternate']
    members = expr['data']

    ret = mcgen('''

const int %(name)s_qtypes[QTYPE_MAX] = {
''',
                name=c_name(name))

    for key in members:
        qtype = find_alternate_member_qtype(members[key])
        assert qtype, "Invalid alternate member"

        ret += mcgen('''
    [%(qtype)s] = %(enum_const)s,
''',
                     qtype = qtype,
                     enum_const = c_enum_const(name + 'Kind', key))

    ret += mcgen('''
};
''')
    return ret


def generate_union(expr, meta):

    name = c_name(expr[meta])
    typeinfo = expr['data']

    base = expr.get('base')
    discriminator = expr.get('discriminator')

    enum_define = discriminator_find_enum_define(expr)
    if enum_define:
        discriminator_type_name = enum_define['enum_name']
    else:
        discriminator_type_name = '%sKind' % (name)

    ret = mcgen('''

struct %(name)s {
''',
                name=name)
    if base:
        ret += mcgen('''
    /* Members inherited from %(c_name)s: */
''',
                     c_name=c_name(base))
        base_fields = find_struct(base)['data']
        ret += generate_struct_fields(base_fields)
        ret += mcgen('''
    /* Own members: */
''')
    else:
        assert not discriminator
        ret += mcgen('''
    %(discriminator_type_name)s kind;
''',
                     discriminator_type_name=c_name(discriminator_type_name))

    # FIXME: What purpose does data serve, besides preventing a union that
    # has a branch named 'data'? We use it in qapi-visit.py to decide
    # whether to bypass the switch statement if visiting the discriminator
    # failed; but since we 0-initialize structs, and cannot tell what
    # branch of the union is in use if the discriminator is invalid, there
    # should not be any data leaks even without a data pointer.  Or, if
    # 'data' is merely added to guarantee we don't have an empty union,
    # shouldn't we enforce that at .json parse time?
    ret += mcgen('''
    union { /* union tag is @%(c_name)s */
        void *data;
''',
                 c_name=c_name(discriminator or 'kind'))

    for key in typeinfo:
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=c_type(typeinfo[key]),
                     c_name=c_name(key))

    ret += mcgen('''
    };
};
''')
    if meta == 'alternate':
        ret += mcgen('''
extern const int %(name)s_qtypes[];
''',
            name=name)


    return ret

def generate_type_cleanup_decl(name):
    ret = mcgen('''
void qapi_free_%(name)s(%(c_type)s obj);
''',
                c_type=c_type(name), name=c_name(name))
    return ret

def generate_type_cleanup(name):
    ret = mcgen('''

void qapi_free_%(name)s(%(c_type)s obj)
{
    QapiDeallocVisitor *md;
    Visitor *v;

    if (!obj) {
        return;
    }

    md = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(md);
    visit_type_%(name)s(v, &obj, NULL, NULL);
    qapi_dealloc_visitor_cleanup(md);
}
''',
                c_type=c_type(name), name=c_name(name))
    return ret

do_builtins = False

(input_file, output_dir, do_c, do_h, prefix, opts) = \
    parse_command_line("b", ["builtins"])

for o, a in opts:
    if o in ("-b", "--builtins"):
        do_builtins = True

c_comment = '''
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
'''
h_comment = '''
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
'''

(fdef, fdecl) = open_output(output_dir, do_c, do_h, prefix,
                            'qapi-types.c', 'qapi-types.h',
                            c_comment, h_comment)

fdef.write(mcgen('''
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include <stdbool.h>
#include <stdint.h>
'''))

exprs = QAPISchema(input_file).get_exprs()

fdecl.write(guardstart("QAPI_TYPES_BUILTIN_STRUCT_DECL"))
for typename in builtin_types.keys():
    fdecl.write(generate_fwd_builtin(typename))
fdecl.write(guardend("QAPI_TYPES_BUILTIN_STRUCT_DECL"))

for expr in exprs:
    ret = ""
    if expr.has_key('struct'):
        ret += generate_fwd_struct(expr['struct'])
    elif expr.has_key('enum'):
        ret += generate_enum(expr['enum'], expr['data'],
                             expr.get('prefix'))
        ret += generate_fwd_enum_struct(expr['enum'])
        fdef.write(generate_enum_lookup(expr['enum'], expr['data'],
                                        expr.get('prefix')))
    elif expr.has_key('union'):
        ret += generate_fwd_struct(expr['union'])
        enum_define = discriminator_find_enum_define(expr)
        if not enum_define:
            ret += generate_enum('%sKind' % expr['union'], expr['data'].keys())
            fdef.write(generate_enum_lookup('%sKind' % expr['union'],
                                            expr['data'].keys()))
    elif expr.has_key('alternate'):
        ret += generate_fwd_struct(expr['alternate'])
        ret += generate_enum('%sKind' % expr['alternate'], expr['data'].keys())
        fdef.write(generate_enum_lookup('%sKind' % expr['alternate'],
                                        expr['data'].keys()))
        fdef.write(generate_alternate_qtypes(expr))
    else:
        continue
    fdecl.write(ret)

# to avoid header dependency hell, we always generate declarations
# for built-in types in our header files and simply guard them
fdecl.write(guardstart("QAPI_TYPES_BUILTIN_CLEANUP_DECL"))
for typename in builtin_types.keys():
    fdecl.write(generate_type_cleanup_decl(typename + "List"))
fdecl.write(guardend("QAPI_TYPES_BUILTIN_CLEANUP_DECL"))

# ...this doesn't work for cases where we link in multiple objects that
# have the functions defined, so we use -b option to provide control
# over these cases
if do_builtins:
    for typename in builtin_types.keys():
        fdef.write(generate_type_cleanup(typename + "List"))

for expr in exprs:
    ret = ""
    if expr.has_key('struct'):
        ret += generate_struct(expr) + "\n"
        ret += generate_type_cleanup_decl(expr['struct'] + "List")
        fdef.write(generate_type_cleanup(expr['struct'] + "List"))
        ret += generate_type_cleanup_decl(expr['struct'])
        fdef.write(generate_type_cleanup(expr['struct']))
    elif expr.has_key('union'):
        ret += generate_union(expr, 'union') + "\n"
        ret += generate_type_cleanup_decl(expr['union'] + "List")
        fdef.write(generate_type_cleanup(expr['union'] + "List"))
        ret += generate_type_cleanup_decl(expr['union'])
        fdef.write(generate_type_cleanup(expr['union']))
    elif expr.has_key('alternate'):
        ret += generate_union(expr, 'alternate') + "\n"
        ret += generate_type_cleanup_decl(expr['alternate'] + "List")
        fdef.write(generate_type_cleanup(expr['alternate'] + "List"))
        ret += generate_type_cleanup_decl(expr['alternate'])
        fdef.write(generate_type_cleanup(expr['alternate']))
    elif expr.has_key('enum'):
        ret += "\n" + generate_type_cleanup_decl(expr['enum'] + "List")
        fdef.write(generate_type_cleanup(expr['enum'] + "List"))
    else:
        continue
    fdecl.write(ret)

close_output(fdef, fdecl)
