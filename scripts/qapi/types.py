"""
QAPI types generator

Copyright IBM, Corp. 2011
Copyright (c) 2013-2018 Red Hat Inc.

Authors:
 Anthony Liguori <aliguori@us.ibm.com>
 Michael Roth <mdroth@linux.vnet.ibm.com>
 Markus Armbruster <armbru@redhat.com>

This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""

from qapi.common import *


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
            if isinstance(v.type, QAPISchemaObjectType):
                ret += gen_object(v.type.name, v.type.base,
                                  v.type.local_members, v.type.variants)

    ret += mcgen('''

struct %(c_name)s {
''',
                 c_name=c_name(name))

    if base:
        if not base.is_implicit():
            ret += mcgen('''
    /* Members inherited from %(c_name)s: */
''',
                         c_name=base.c_name())
        ret += gen_struct_members(base.members)
        if not base.is_implicit():
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
    if (not base or base.is_empty()) and not members and not variants:
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
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=var.type.c_unboxed_type(),
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
    Visitor *v;

    if (!obj) {
        return;
    }

    v = qapi_dealloc_visitor_new();
    visit_type_%(c_name)s(v, NULL, &obj, NULL);
    visit_free(v);
}
''',
                c_name=c_name(name))
    return ret


class QAPISchemaGenTypeVisitor(QAPISchemaMonolithicCVisitor):

    def __init__(self, prefix, opt_builtins):
        QAPISchemaMonolithicCVisitor.__init__(
            self, prefix, 'qapi-types', ' * Schema-defined QAPI types',
            __doc__)
        self._opt_builtins = opt_builtins
        self._genc.preamble_add(mcgen('''
#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
''',
                                      prefix=prefix))
        self._genh.preamble_add(mcgen('''
#include "qapi/util.h"
'''))
        self._btin = '\n' + guardstart('QAPI_TYPES_BUILTIN')

    def visit_begin(self, schema):
        # gen_object() is recursive, ensure it doesn't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)

    def visit_end(self):
        # To avoid header dependency hell, we always generate
        # declarations for built-in types in our header files and
        # simply guard them.  See also opt_builtins (command line
        # option -b).
        self._btin += guardend('QAPI_TYPES_BUILTIN')
        self._genh.preamble_add(self._btin)
        self._btin = None

    def _gen_type_cleanup(self, name):
        self._genh.add(gen_type_cleanup_decl(name))
        self._genc.add(gen_type_cleanup(name))

    def visit_enum_type(self, name, info, values, prefix):
        # Special case for our lone builtin enum type
        # TODO use something cleaner than existence of info
        if not info:
            self._btin += gen_enum(name, values, prefix)
            if self._opt_builtins:
                self._genc.add(gen_enum_lookup(name, values, prefix))
        else:
            self._genh.preamble_add(gen_enum(name, values, prefix))
            self._genc.add(gen_enum_lookup(name, values, prefix))

    def visit_array_type(self, name, info, element_type):
        if isinstance(element_type, QAPISchemaBuiltinType):
            self._btin += gen_fwd_object_or_array(name)
            self._btin += gen_array(name, element_type)
            self._btin += gen_type_cleanup_decl(name)
            if self._opt_builtins:
                self._genc.add(gen_type_cleanup(name))
        else:
            self._genh.preamble_add(gen_fwd_object_or_array(name))
            self._genh.add(gen_array(name, element_type))
            self._gen_type_cleanup(name)

    def visit_object_type(self, name, info, base, members, variants):
        # Nothing to do for the special empty builtin
        if name == 'q_empty':
            return
        self._genh.preamble_add(gen_fwd_object_or_array(name))
        self._genh.add(gen_object(name, base, members, variants))
        if base and not base.is_implicit():
            self._genh.add(gen_upcast(name, base))
        # TODO Worth changing the visitor signature, so we could
        # directly use rather than repeat type.is_implicit()?
        if not name.startswith('q_'):
            # implicit types won't be directly allocated/freed
            self._gen_type_cleanup(name)

    def visit_alternate_type(self, name, info, variants):
        self._genh.preamble_add(gen_fwd_object_or_array(name))
        self._genh.add(gen_object(name, None,
                                  [variants.tag_member], variants))
        self._gen_type_cleanup(name)


def gen_types(schema, output_dir, prefix, opt_builtins):
    vis = QAPISchemaGenTypeVisitor(prefix, opt_builtins)
    schema.visit(vis)
    vis.write(output_dir)
