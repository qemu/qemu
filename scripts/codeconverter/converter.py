#!/usr/bin/env python3
# QEMU library
#
# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
import sys
import argparse
import os
import os.path
import re
from typing import *

from codeconverter.patching import FileInfo, match_class_dict, FileList
import codeconverter.qom_macros
from codeconverter.qom_type_info import TI_FIELDS, type_infos, TypeInfoVar

import logging
logger = logging.getLogger(__name__)
DBG = logger.debug
INFO = logger.info
WARN = logger.warning

def process_all_files(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    DBG("filenames: %r", args.filenames)

    files = FileList()
    files.extend(FileInfo(files, fn, args.force) for fn in args.filenames)
    for f in files:
        DBG('opening %s', f.filename)
        f.load()

    if args.table:
        fields = ['filename', 'variable_name'] + TI_FIELDS
        print('\t'.join(fields))
        for f in files:
            for t in f.matches_of_type(TypeInfoVar):
                assert isinstance(t, TypeInfoVar)
                values = [f.filename, t.name] + \
                         [t.get_raw_initializer_value(f)
                          for f in TI_FIELDS]
                DBG('values: %r', values)
                assert all('\t' not in v for v in values)
                values = [v.replace('\n', ' ').replace('"', '') for v in values]
                print('\t'.join(values))
        return

    match_classes = match_class_dict()
    if not args.patterns:
        parser.error("--pattern is required")

    classes = [p for arg in args.patterns
               for p in re.split(r'[\s,]', arg)
               if p.strip()]
    for c in classes:
        if c not in match_classes \
           or not match_classes[c].regexp:
            print("Invalid pattern name: %s" % (c), file=sys.stderr)
            print("Valid patterns:", file=sys.stderr)
            print(PATTERN_HELP, file=sys.stderr)
            sys.exit(1)

    DBG("classes: %r", classes)
    files.patch_content(max_passes=args.passes, class_names=classes)

    for f in files:
        #alltypes.extend(f.type_infos)
        #full_types.extend(f.full_types())

        if not args.dry_run:
            if args.inplace:
                f.patch_inplace()
            if args.diff:
                f.show_diff()
            if not args.diff and not args.inplace:
                f.write_to_file(sys.stdout)
                sys.stdout.flush()


PATTERN_HELP = ('\n'.join("  %s: %s" % (n, str(c.__doc__).strip())
                for (n,c) in sorted(match_class_dict().items())
                if c.has_replacement_rule()))

def main() -> None:
    p = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('filenames', nargs='+')
    p.add_argument('--passes', type=int, default=1,
                   help="Number of passes (0 means unlimited)")
    p.add_argument('--pattern', required=True, action='append',
                   default=[], dest='patterns',
                   help="Pattern to scan for")
    p.add_argument('--inplace', '-i', action='store_true',
                   help="Patch file in place")
    p.add_argument('--dry-run', action='store_true',
                   help="Don't patch files or print patching results")
    p.add_argument('--force', '-f', action='store_true',
                   help="Perform changes even if not completely safe")
    p.add_argument('--diff', action='store_true',
                   help="Print diff output on stdout")
    p.add_argument('--debug', '-d', action='store_true',
                   help="Enable debugging")
    p.add_argument('--verbose', '-v', action='store_true',
                   help="Verbose logging on stderr")
    p.add_argument('--table', action='store_true',
                   help="Print CSV table of type information")
    p.add_argument_group("Valid pattern names",
                         PATTERN_HELP)
    args = p.parse_args()

    loglevel = (logging.DEBUG if args.debug
             else logging.INFO if args.verbose
             else logging.WARN)
    logging.basicConfig(format='%(levelname)s: %(message)s', level=loglevel)
    DBG("args: %r", args)
    process_all_files(p, args)

if __name__ == '__main__':
    main()