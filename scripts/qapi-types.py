#
# QAPI types generator
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2016 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *


# variants must be emitted before their container; track what has already
# been output
objects_seen = set()


def gen_fwd_object_or_array(name):
    return mcgen('''

typedef struct %(c_name)s %(c_name)s;
''',
                 c_name=c_name(name))


def gen_array(name, element_type):
    return mcgen('''

struct %(c_name)s {
    %(c_name)s *next;
    %(c_type)s value;
};
''',
                 c_name=c_name(name), c_type=element_type.c_type())


def gen_struct_members(members):
    ret = ''
    for memb in members:
        if memb.optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_name(memb.name))
        ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=memb.type.c_type(), c_name=c_name(memb.name))
    return ret


def gen_object(name, base, members, variants):
    if name in objects_seen:
        return ''
    objects_seen.add(name)

    ret = ''
    if variants:
        for v in variants.variants:
            if (isinstance(v.type, QAPISchemaObjectType) and
                    not v.type.is_implicit()):
                ret += gen_object(v.type.name, v.type.base,
                                  v.type.local_members, v.type.variants)

    ret += mcgen('''

struct %(c_name)s {
''',
                 c_name=c_name(name))

    if base:
        ret += mcgen('''
    /* Members inherited from %(c_name)s: */
''',
                     c_name=base.c_name())
        ret += gen_struct_members(base.members)
        ret += mcgen('''
    /* Own members: */
''')
    ret += gen_struct_members(members)

    if variants:
        ret += gen_variants(variants)

    # Make sure that all structs have at least one member; this avoids
    # potential issues with attempting to malloc space for zero-length
    # structs in C, and also incompatibility with C++ (where an empty
    # struct is size 1).
    if not (base and base.members) and not members and not variants:
        ret += mcgen('''
    char qapi_dummy_for_empty_struct;
''')

    ret += mcgen('''
};
''')

    return ret


def gen_upcast(name, base):
    # C makes const-correctness ugly.  We have to cast away const to let
    # this function work for both const and non-const obj.
    return mcgen('''

static inline %(base)s *qapi_%(c_name)s_base(const %(c_name)s *obj)
{
    return (%(base)s *)obj;
}
''',
                 c_name=c_name(name), base=base.c_name())


def gen_variants(variants):
    ret = mcgen('''
    union { /* union tag is @%(c_name)s */
''',
                c_name=c_name(variants.tag_member.name))

    for var in variants.variants:
        # Ugly special case for simple union TODO get rid of it
        simple_union_type = var.simple_union_type()
        typ = simple_union_type or var.type
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=typ.c_type(is_unboxed=not simple_union_type),
                     c_name=c_name(var.name))

    ret += mcgen('''
    } u;
''')

    return ret


def gen_type_cleanup_decl(name):
    ret = mcgen('''

void qapi_free_%(c_name)s(%(c_name)s *obj);
''',
                c_name=c_name(name))
    return ret


def gen_type_cleanup(name):
    ret = mcgen('''

void qapi_free_%(c_name)s(%(c_name)s *obj)
{
    QapiDeallocVisitor *qdv;
    Visitor *v;

    if (!obj) {
        return;
    }

    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_%(c_name)s(v, NULL, &obj, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}
''',
                c_name=c_name(name))
    return ret


class QAPISchemaGenTypeVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._fwdecl = None
        self._btin = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._fwdecl = ''
        self._btin = guardstart('QAPI_TYPES_BUILTIN')

    def visit_end(self):
        self.decl = self._fwdecl + self.decl
        self._fwdecl = None
        # To avoid header dependency hell, we always generate
        # declarations for built-in types in our header files and
        # simply guard them.  See also do_builtins (command line
        # option -b).
        self._btin += guardend('QAPI_TYPES_BUILTIN')
        self.decl = self._btin + self.decl
        self._btin = None

    def visit_needed(self, entity):
        # Visit everything except implicit objects
        return not (entity.is_implicit() and
                    isinstance(entity, QAPISchemaObjectType))

    def _gen_type_cleanup(self, name):
        self.decl += gen_type_cleanup_decl(name)
        self.defn += gen_type_cleanup(name)

    def visit_enum_type(self, name, info, values, prefix):
        # Special case for our lone builtin enum type
        # TODO use something cleaner than existence of info
        if not info:
            self._btin += gen_enum(name, values, prefix)
            if do_builtins:
                self.defn += gen_enum_lookup(name, values, prefix)
        else:
            self._fwdecl += gen_enum(name, values, prefix)
            self.defn += gen_enum_lookup(name, values, prefix)

    def visit_array_type(self, name, info, element_type):
        if isinstance(element_type, QAPISchemaBuiltinType):
            self._btin += gen_fwd_object_or_array(name)
            self._btin += gen_array(name, element_type)
            self._btin += gen_type_cleanup_decl(name)
            if do_builtins:
                self.defn += gen_type_cleanup(name)
        else:
            self._fwdecl += gen_fwd_object_or_array(name)
            self.decl += gen_array(name, element_type)
            self._gen_type_cleanup(name)

    def visit_object_type(self, name, info, base, members, variants):
        self._fwdecl += gen_fwd_object_or_array(name)
        self.decl += gen_object(name, base, members, variants)
        if base:
            self.decl += gen_upcast(name, base)
        self._gen_type_cleanup(name)

    def visit_alternate_type(self, name, info, variants):
        self._fwdecl += gen_fwd_object_or_array(name)
        self.decl += gen_object(name, None, [variants.tag_member], variants)
        self._gen_type_cleanup(name)

# If you link code generated from multiple schemata, you want only one
# instance of the code for built-in types.  Generate it only when
# do_builtins, enabled by command line option -b.  See also
# QAPISchemaGenTypeVisitor.visit_end().
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
#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
''',
                 prefix=prefix))

# To avoid circular headers, use only typedefs.h here, not qobject.h
fdecl.write(mcgen('''
#include "qemu/typedefs.h"
'''))

schema = QAPISchema(input_file)
gen = QAPISchemaGenTypeVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
