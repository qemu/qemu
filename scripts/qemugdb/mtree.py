#!/usr/bin/python

# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Contributions after 2012-01-13 are licensed under the terms of the
# GNU GPL, version 2 or (at your option) any later version.

# 'qemu mtree' -- display the memory hierarchy

import gdb

def isnull(ptr):
    return ptr == gdb.Value(0).cast(ptr.type)

def int128(p):
    '''Read an Int128 type to a python integer.

    QEMU can be built with native Int128 support so we need to detect
    if the value is a structure or the native type.
    '''
    if p.type.code == gdb.TYPE_CODE_STRUCT:
        return int(p['lo']) + (int(p['hi']) << 64)
    else:
        return int(("%s" % p), 16)

class MtreeCommand(gdb.Command):
    '''Display the memory tree hierarchy'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu mtree', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)
        self.queue = []
    def invoke(self, arg, from_tty):
        self.seen = set()
        self.queue_root('address_space_memory')
        self.queue_root('address_space_io')
        self.process_queue()
    def queue_root(self, varname):
        ptr = gdb.parse_and_eval(varname)['root']
        self.queue.append(ptr)
    def process_queue(self):
        while self.queue:
            ptr = self.queue.pop(0)
            if int(ptr) in self.seen:
                continue
            self.print_item(ptr)
    def print_item(self, ptr, offset = gdb.Value(0), level = 0):
        self.seen.add(int(ptr))
        addr = ptr['addr']
        addr += offset
        size = int128(ptr['size'])
        alias = ptr['alias']
        klass = ''
        if not isnull(alias):
            klass = ' (alias)'
        elif not isnull(ptr['ops']):
            klass = ' (I/O)'
        elif bool(ptr['ram']):
            klass = ' (RAM)'
        gdb.write('%s%016x-%016x %s%s (@ %s)\n'
                  % ('  ' * level,
                     int(addr),
                     int(addr + (size - 1)),
                     ptr['name'].string(),
                     klass,
                     ptr,
                     ),
                  gdb.STDOUT)
        if not isnull(alias):
            gdb.write('%s    alias: %s@%016x (@ %s)\n' %
                      ('  ' * level,
                       alias['name'].string(),
                       int(ptr['alias_offset']),
                       alias,
                       ),
                      gdb.STDOUT)
            self.queue.append(alias)
        subregion = ptr['subregions']['tqh_first']
        level += 1
        while not isnull(subregion):
            self.print_item(subregion, addr, level)
            subregion = subregion['subregions_link']['tqe_next']

