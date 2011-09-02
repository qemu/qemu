#
# QAPI types generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *
import sys
import os
import getopt
import errno

def generate_fwd_struct(name, members):
    return mcgen('''
typedef struct %(name)s %(name)s;

typedef struct %(name)sList
{
    %(name)s *value;
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=name)

def generate_struct(structname, fieldname, members):
    ret = mcgen('''
struct %(name)s
{
''',
          name=structname)

    for argname, argentry, optional, structured in parse_args(members):
        if optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_var(argname))
        if structured:
            push_indent()
            ret += generate_struct("", argname, argentry)
            pop_indent()
        else:
            ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=c_type(argentry), c_name=c_var(argname))

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
                     value=c_var(value).lower())

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

    i = 0
    for value in values:
        enum_decl += mcgen('''
    %(abbrev)s_%(value)s = %(i)d,
''',
                     abbrev=de_camel_case(name).upper(),
                     value=c_var(value).upper(),
                     i=i)
        i += 1

    enum_decl += mcgen('''
} %(name)s;
''',
                 name=name)

    return lookup_decl + enum_decl

def generate_union(name, typeinfo):
    ret = mcgen('''
struct %(name)s
{
    %(name)sKind kind;
    union {
''',
                name=name)

    for key in typeinfo:
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=c_type(typeinfo[key]),
                     c_name=c_var(key))

    ret += mcgen('''
    };
};
''')

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
    opts, args = getopt.gnu_getopt(sys.argv[1:], "p:o:", ["prefix=", "output-dir="])
except getopt.GetoptError, err:
    print str(err)
    sys.exit(1)

output_dir = ""
prefix = ""
c_file = 'qapi-types.c'
h_file = 'qapi-types.h'

for o, a in opts:
    if o in ("-p", "--prefix"):
        prefix = a
    elif o in ("-o", "--output-dir"):
        output_dir = a + "/"

c_file = output_dir + prefix + c_file
h_file = output_dir + prefix + h_file

try:
    os.makedirs(output_dir)
except os.error, e:
    if e.errno != errno.EEXIST:
        raise

fdef = open(c_file, 'w')
fdecl = open(h_file, 'w')

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

#include "qapi/qapi-dealloc-visitor.h"
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

#include "qapi/qapi-types-core.h"
''',
                  guard=guardname(h_file)))

exprs = parse_schema(sys.stdin)

for expr in exprs:
    ret = "\n"
    if expr.has_key('type'):
        ret += generate_fwd_struct(expr['type'], expr['data'])
    elif expr.has_key('enum'):
        ret += generate_enum(expr['enum'], expr['data'])
        fdef.write(generate_enum_lookup(expr['enum'], expr['data']))
    elif expr.has_key('union'):
        ret += generate_fwd_struct(expr['union'], expr['data']) + "\n"
        ret += generate_enum('%sKind' % expr['union'], expr['data'].keys())
    else:
        continue
    fdecl.write(ret)

for expr in exprs:
    ret = "\n"
    if expr.has_key('type'):
        ret += generate_struct(expr['type'], "", expr['data']) + "\n"
        ret += generate_type_cleanup_decl(expr['type'])
        fdef.write(generate_type_cleanup(expr['type']) + "\n")
    elif expr.has_key('union'):
        ret += generate_union(expr['union'], expr['data'])
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
