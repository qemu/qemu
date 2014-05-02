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
import sys

try:
    exprs = parse_schema(sys.stdin)
except SystemExit:
    raise

pprint(exprs)
pprint(enum_types)
pprint(struct_types)
