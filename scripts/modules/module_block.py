#!/usr/bin/python
#
# Module information generator
#
# Copyright Red Hat, Inc. 2015 - 2016
#
# Authors:
#  Marc Mari <markmb@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from __future__ import print_function
import sys
import os

def get_string_struct(line):
    data = line.split()

    # data[0] -> struct element name
    # data[1] -> =
    # data[2] -> value

    return data[2].replace('"', '')[:-1]

def add_module(fheader, library, format_name, protocol_name):
    lines = []
    lines.append('.library_name = "' + library + '",')
    if format_name != "":
        lines.append('.format_name = "' + format_name + '",')
    if protocol_name != "":
        lines.append('.protocol_name = "' + protocol_name + '",')

    text = '\n        '.join(lines)
    fheader.write('\n    {\n        ' + text + '\n    },')

def process_file(fheader, filename):
    # This parser assumes the coding style rules are being followed
    with open(filename, "r") as cfile:
        found_start = False
        library, _ = os.path.splitext(os.path.basename(filename))
        for line in cfile:
            if found_start:
                line = line.replace('\n', '')
                if line.find(".format_name") != -1:
                    format_name = get_string_struct(line)
                elif line.find(".protocol_name") != -1:
                    protocol_name = get_string_struct(line)
                elif line == "};":
                    add_module(fheader, library, format_name, protocol_name)
                    found_start = False
            elif line.find("static BlockDriver") != -1:
                found_start = True
                format_name = ""
                protocol_name = ""

def print_top(fheader):
    fheader.write('''/* AUTOMATICALLY GENERATED, DO NOT MODIFY */
/*
 * QEMU Block Module Infrastructure
 *
 * Authors:
 *  Marc Mari       <markmb@redhat.com>
 */

''')

    fheader.write('''#ifndef QEMU_MODULE_BLOCK_H
#define QEMU_MODULE_BLOCK_H

static const struct {
    const char *format_name;
    const char *protocol_name;
    const char *library_name;
} block_driver_modules[] = {''')

def print_bottom(fheader):
    fheader.write('''
};

#endif
''')

# First argument: output file
# All other arguments: modules source files (.c)
output_file = sys.argv[1]
with open(output_file, 'w') as fheader:
    print_top(fheader)

    for filename in sys.argv[2:]:
        if os.path.isfile(filename):
            process_file(fheader, filename)
        else:
            print("File " + filename + " does not exist.", file=sys.stderr)
            sys.exit(1)

    print_bottom(fheader)

sys.exit(0)
