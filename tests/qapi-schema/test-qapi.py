#!/usr/bin/env python3
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


import argparse
import difflib
import os
import sys
from io import StringIO

from qapi.error import QAPIError
from qapi.schema import QAPISchema, QAPISchemaVisitor


class QAPISchemaTestVisitor(QAPISchemaVisitor):

    def visit_module(self, name):
        print('module %s' % name)

    def visit_include(self, name, info):
        print('include %s' % name)

    def visit_enum_type(self, name, info, ifcond, features, members, prefix):
        print('enum %s' % name)
        if prefix:
            print('    prefix %s' % prefix)
        for m in members:
            print('    member %s' % m.name)
            self._print_if(m.ifcond, indent=8)
            self._print_features(m.features, indent=8)
        self._print_if(ifcond)
        self._print_features(features)

    def visit_array_type(self, name, info, ifcond, element_type):
        if not info:
            return              # suppress built-in arrays
        print('array %s %s' % (name, element_type.name))
        self._print_if(ifcond)

    def visit_object_type(self, name, info, ifcond, features,
                          base, members, branches):
        print('object %s' % name)
        if base:
            print('    base %s' % base.name)
        for m in members:
            print('    member %s: %s optional=%s'
                  % (m.name, m.type.name, m.optional))
            self._print_if(m.ifcond, 8)
            self._print_features(m.features, indent=8)
        self._print_variants(branches)
        self._print_if(ifcond)
        self._print_features(features)

    def visit_alternate_type(self, name, info, ifcond, features,
                             alternatives):
        print('alternate %s' % name)
        self._print_variants(alternatives)
        self._print_if(ifcond)
        self._print_features(features)

    def visit_command(self, name, info, ifcond, features,
                      arg_type, ret_type, gen, success_response, boxed,
                      allow_oob, allow_preconfig, coroutine):
        print('command %s %s -> %s'
              % (name, arg_type and arg_type.name,
                 ret_type and ret_type.name))
        print('    gen=%s success_response=%s boxed=%s oob=%s preconfig=%s%s'
              % (gen, success_response, boxed, allow_oob, allow_preconfig,
                 " coroutine=True" if coroutine else ""))
        self._print_if(ifcond)
        self._print_features(features)

    def visit_event(self, name, info, ifcond, features, arg_type, boxed):
        print('event %s %s' % (name, arg_type and arg_type.name))
        print('    boxed=%s' % boxed)
        self._print_if(ifcond)
        self._print_features(features)

    @staticmethod
    def _print_variants(variants):
        if variants:
            print('    tag %s' % variants.tag_member.name)
            for v in variants.variants:
                print('    case %s: %s' % (v.name, v.type.name))
                QAPISchemaTestVisitor._print_if(v.ifcond, indent=8)

    @staticmethod
    def _print_if(ifcond, indent=4):
        if ifcond.is_present():
            print('%sif %s' % (' ' * indent, ifcond.ifcond))

    @classmethod
    def _print_features(cls, features, indent=4):
        if features:
            for f in features:
                print('%sfeature %s' % (' ' * indent, f.name))
                cls._print_if(f.ifcond, indent + 4)


def test_frontend(fname):
    schema = QAPISchema(fname)
    schema.visit(QAPISchemaTestVisitor())

    for doc in schema.docs:
        if doc.symbol:
            print('doc symbol=%s' % doc.symbol)
        else:
            print('doc freeform')
        print('    body=\n%s' % doc.body.text)
        for arg, section in doc.args.items():
            print('    arg=%s\n%s' % (arg, section.text))
        for feat, section in doc.features.items():
            print('    feature=%s\n%s' % (feat, section.text))
        for section in doc.sections:
            print('    section=%s\n%s' % (section.kind, section.text))


def open_test_result(dir_name, file_name, update):
    mode = 'r+' if update else 'r'
    try:
        return open(os.path.join(dir_name, file_name), mode, encoding='utf-8')
    except FileNotFoundError:
        if not update:
            raise
    return open(os.path.join(dir_name, file_name), 'w+', encoding='utf-8')


def test_and_diff(test_name, dir_name, update):
    sys.stdout = StringIO()
    try:
        test_frontend(os.path.join(dir_name, test_name + '.json'))
    except QAPIError as err:
        errstr = str(err) + '\n'
        if dir_name:
            errstr = errstr.replace(dir_name + '/', '')
        actual_err = errstr.splitlines(True)
    else:
        actual_err = []
    finally:
        actual_out = sys.stdout.getvalue().splitlines(True)
        sys.stdout.close()
        sys.stdout = sys.__stdout__

    try:
        outfp = open_test_result(dir_name, test_name + '.out', update)
        errfp = open_test_result(dir_name, test_name + '.err', update)
        expected_out = outfp.readlines()
        expected_err = errfp.readlines()
    except OSError as err:
        print("%s: can't open '%s': %s"
              % (sys.argv[0], err.filename, err.strerror),
              file=sys.stderr)
        return 2

    if actual_out == expected_out and actual_err == expected_err:
        return 0

    print("%s: %s" % (test_name, 'UPDATE' if update else 'FAIL'),
          file=sys.stderr)
    out_diff = difflib.unified_diff(expected_out, actual_out, outfp.name)
    err_diff = difflib.unified_diff(expected_err, actual_err, errfp.name)
    sys.stdout.writelines(out_diff)
    sys.stdout.writelines(err_diff)

    if not update:
        print(("\n%s: set QEMU_TEST_REGENERATE=1 to recreate reference output" +
               "if the QAPI schema generator was intentionally changed") % test_name,
              file=sys.stderr)
        return 1

    try:
        outfp.truncate(0)
        outfp.seek(0)
        outfp.writelines(actual_out)
        errfp.truncate(0)
        errfp.seek(0)
        errfp.writelines(actual_err)
    except OSError as err:
        print("%s: can't write '%s': %s"
              % (sys.argv[0], err.filename, err.strerror),
              file=sys.stderr)
        return 2

    return 0


def main(argv):
    parser = argparse.ArgumentParser(
        description='QAPI schema tester')
    parser.add_argument('-d', '--dir', action='store', default='',
                        help="directory containing tests")
    parser.add_argument('-u', '--update', action='store_true',
                        default='QEMU_TEST_REGENERATE' in os.environ,
                        help="update expected test results")
    parser.add_argument('tests', nargs='*', metavar='TEST', action='store')
    args = parser.parse_args()

    status = 0
    for t in args.tests:
        (dir_name, base_name) = os.path.split(t)
        dir_name = dir_name or args.dir
        test_name = os.path.splitext(base_name)[0]
        status |= test_and_diff(test_name, dir_name, args.update)

    sys.exit(status)


if __name__ == '__main__':
    main(sys.argv)
    sys.exit(0)
