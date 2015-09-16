#
# QAPI types generator
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2015 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *


def gen_fwd_object_or_array(name):
    return mcgen('''

typedef struct %(c_name)s %(c_name)s;
''',
                 c_name=c_name(name))


def gen_array(name, element_type):
    return mcgen('''

struct %(c_name)s {
    union {
        %(c_type)s value;
        uint64_t padding;
    };
    %(c_name)s *next;
};
''',
                 c_name=c_name(name), c_type=element_type.c_type())


def gen_struct_field(name, typ, optional):
    ret = ''

    if optional:
        ret += mcgen('''
    bool has_%(c_name)s;
''',
                     c_name=c_name(name))
    ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                 c_type=typ.c_type(), c_name=c_name(name))
    return ret


def gen_struct_fields(members):
    ret = ''

    for memb in members:
        ret += gen_struct_field(memb.name, memb.type, memb.optional)
    return ret


def gen_struct(name, base, members):
    ret = mcgen('''

struct %(c_name)s {
''',
                c_name=c_name(name))

    if base:
        ret += gen_struct_field('base', base, False)

    ret += gen_struct_fields(members)

    # Make sure that all structs have at least one field; this avoids
    # potential issues with attempting to malloc space for zero-length
    # structs in C, and also incompatibility with C++ (where an empty
    # struct is size 1).
    if not base and not members:
        ret += mcgen('''
    char qapi_dummy_field_for_empty_struct;
''')

    ret += mcgen('''
};
''')

    return ret


def gen_alternate_qtypes_decl(name):
    return mcgen('''

extern const int %(c_name)s_qtypes[];
''',
                 c_name=c_name(name))


def gen_alternate_qtypes(name, variants):
    ret = mcgen('''

const int %(c_name)s_qtypes[QTYPE_MAX] = {
''',
                c_name=c_name(name))

    for var in variants.variants:
        qtype = var.type.alternate_qtype()
        assert qtype

        ret += mcgen('''
    [%(qtype)s] = %(enum_const)s,
''',
                     qtype=qtype,
                     enum_const=c_enum_const(variants.tag_member.type.name,
                                             var.name))

    ret += mcgen('''
};
''')
    return ret


def gen_union(name, base, variants):
    ret = mcgen('''

struct %(c_name)s {
''',
                c_name=c_name(name))
    if base:
        ret += mcgen('''
    /* Members inherited from %(c_name)s: */
''',
                     c_name=c_name(base.name))
        ret += gen_struct_fields(base.members)
        ret += mcgen('''
    /* Own members: */
''')
    else:
        ret += mcgen('''
    %(c_type)s kind;
''',
                     c_type=c_name(variants.tag_member.type.name))

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
                 # TODO ugly special case for simple union
                 # Use same tag name in C as on the wire to get rid of
                 # it, then: c_name=c_name(variants.tag_member.name)
                 c_name=c_name(variants.tag_name or 'kind'))

    for var in variants.variants:
        # Ugly special case for simple union TODO get rid of it
        typ = var.simple_union_type() or var.type
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=typ.c_type(),
                     c_name=c_name(var.name))

    ret += mcgen('''
    };
};
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
    QapiDeallocVisitor *md;
    Visitor *v;

    if (!obj) {
        return;
    }

    md = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(md);
    visit_type_%(c_name)s(v, &obj, NULL, NULL);
    qapi_dealloc_visitor_cleanup(md);
}
''',
                c_name=c_name(name))
    return ret


class QAPISchemaGenTypeVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._fwdecl = None
        self._fwdefn = None
        self._btin = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._fwdecl = ''
        self._fwdefn = ''
        self._btin = guardstart('QAPI_TYPES_BUILTIN')

    def visit_end(self):
        self.decl = self._fwdecl + self.decl
        self._fwdecl = None
        self.defn = self._fwdefn + self.defn
        self._fwdefn = None
        # To avoid header dependency hell, we always generate
        # declarations for built-in types in our header files and
        # simply guard them.  See also do_builtins (command line
        # option -b).
        self._btin += guardend('QAPI_TYPES_BUILTIN')
        self.decl = self._btin + self.decl
        self._btin = None

    def _gen_type_cleanup(self, name):
        self.decl += gen_type_cleanup_decl(name)
        self.defn += gen_type_cleanup(name)

    def visit_enum_type(self, name, info, values, prefix):
        self._fwdecl += gen_enum(name, values, prefix)
        self._fwdefn += gen_enum_lookup(name, values, prefix)

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
        if info:
            self._fwdecl += gen_fwd_object_or_array(name)
            if variants:
                assert not members      # not implemented
                self.decl += gen_union(name, base, variants)
            else:
                self.decl += gen_struct(name, base, members)
            self._gen_type_cleanup(name)

    def visit_alternate_type(self, name, info, variants):
        self._fwdecl += gen_fwd_object_or_array(name)
        self._fwdefn += gen_alternate_qtypes(name, variants)
        self.decl += gen_union(name, None, variants)
        self.decl += gen_alternate_qtypes_decl(name)
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
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include <stdbool.h>
#include <stdint.h>
#include "qapi/qmp/qobject.h"
'''))

schema = QAPISchema(input_file)
gen = QAPISchemaGenTypeVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
