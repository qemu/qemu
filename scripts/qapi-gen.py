#!/usr/bin/env python
# QAPI generator
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

import sys
from qapi.common import parse_command_line, QAPISchema
from qapi.types import gen_types
from qapi.visit import gen_visit
from qapi.commands import gen_commands
from qapi.events import gen_events
from qapi.introspect import gen_introspect
from qapi.doc import gen_doc


def main(argv):
    (input_file, output_dir, prefix, opts) = \
        parse_command_line('bu', ['builtins', 'unmask-non-abi-names'])

    opt_builtins = False
    opt_unmask = False

    for o, a in opts:
        if o in ('-b', '--builtins'):
            opt_builtins = True
        if o in ('-u', '--unmask-non-abi-names'):
            opt_unmask = True

    schema = QAPISchema(input_file)

    gen_types(schema, output_dir, prefix, opt_builtins)
    gen_visit(schema, output_dir, prefix, opt_builtins)
    gen_commands(schema, output_dir, prefix)
    gen_events(schema, output_dir, prefix)
    gen_introspect(schema, output_dir, prefix, opt_unmask)
    gen_doc(schema, output_dir, prefix)


if __name__ == '__main__':
    main(sys.argv)
