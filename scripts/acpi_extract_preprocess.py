#!/usr/bin/python
# Copyright (C) 2011 Red Hat, Inc., Michael S. Tsirkin <mst@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.

# Read a preprocessed ASL listing and put each ACPI_EXTRACT
# directive in a comment, to make iasl skip it.
# We also put each directive on a new line, the machinery
# in tools/acpi_extract.py requires this.

import re;
import sys;
import fileinput;

def die(diag):
    sys.stderr.write("Error: %s\n" % (diag))
    sys.exit(1)

# Note: () around pattern make split return matched string as part of list
psplit = re.compile(r''' (
                          \b # At word boundary
                          ACPI_EXTRACT_\w+ # directive
                          \s+ # some whitespace
                          \w+ # array name
                         )''', re.VERBOSE);

lineno = 0
for line in fileinput.input():
    # line number and debug string to output in case of errors
    lineno = lineno + 1
    debug = "input line %d: %s" % (lineno, line.rstrip())

    s = psplit.split(line);
    # The way split works, each odd item is the matching ACPI_EXTRACT directive.
    # Put each in a comment, and on a line by itself.
    for i in range(len(s)):
        if (i % 2):
            sys.stdout.write("\n/* %s */\n" % s[i])
        else:
            sys.stdout.write(s[i])
