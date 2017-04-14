#
# QAPI parser test harness
#
# Copyright (c) 2013 Red Hat Inc.
#
# Authors:
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#

from qapi import *
from pprint import pprint
import os
import sys


class QAPISchemaTestVisitor(QAPISchemaVisitor):
    def visit_enum_type(self, name, info, values, prefix):
        print 'enum %s %s' % (name, values)
        if prefix:
            print '    prefix %s' % prefix

    def visit_object_type(self, name, info, base, members, variants):
        print 'object %s' % name
        if base:
            print '    base %s' % base.name
        for m in members:
            print '    member %s: %s optional=%s' % \
                (m.name, m.type.name, m.optional)
        self._print_variants(variants)

    def visit_alternate_type(self, name, info, variants):
        print 'alternate %s' % name
        self._print_variants(variants)

    def visit_command(self, name, info, arg_type, ret_type,
                      gen, success_response, boxed):
        print 'command %s %s -> %s' % \
            (name, arg_type and arg_type.name, ret_type and ret_type.name)
        print '   gen=%s success_response=%s boxed=%s' % \
            (gen, success_response, boxed)

    def visit_event(self, name, info, arg_type, boxed):
        print 'event %s %s' % (name, arg_type and arg_type.name)
        print '   boxed=%s' % boxed

    @staticmethod
    def _print_variants(variants):
        if variants:
            print '    tag %s' % variants.tag_member.name
            for v in variants.variants:
                print '    case %s: %s' % (v.name, v.type.name)

schema = QAPISchema(sys.argv[1])
schema.visit(QAPISchemaTestVisitor())

for doc in schema.docs:
    if doc.symbol:
        print 'doc symbol=%s' % doc.symbol
    else:
        print 'doc freeform'
    print '    body=\n%s' % doc.body
    for arg, section in doc.args.iteritems():
        print '    arg=%s\n%s' % (arg, section)
    for section in doc.sections:
        print '    section=%s\n%s' % (section.name, section)
