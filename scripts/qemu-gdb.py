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


import gdb

def isnull(ptr):
    return ptr == gdb.Value(0).cast(ptr.type)

def int128(p):
    return long(p['lo']) + (long(p['hi']) << 64)

def get_fs_base():
    '''Fetch %fs base value using arch_prctl(ARCH_GET_FS)'''
    # %rsp - 120 is scratch space according to the SystemV ABI
    old = gdb.parse_and_eval('*(uint64_t*)($rsp - 120)')
    gdb.execute('call arch_prctl(0x1003, $rsp - 120)', False, True)
    fs_base = gdb.parse_and_eval('*(uint64_t*)($rsp - 120)')
    gdb.execute('set *(uint64_t*)($rsp - 120) = %s' % old, False, True)
    return fs_base

def get_glibc_pointer_guard():
    '''Fetch glibc pointer guard value'''
    fs_base = get_fs_base()
    return gdb.parse_and_eval('*(uint64_t*)((uint64_t)%s + 0x30)' % fs_base)

def glibc_ptr_demangle(val, pointer_guard):
    '''Undo effect of glibc's PTR_MANGLE()'''
    return gdb.parse_and_eval('(((uint64_t)%s >> 0x11) | ((uint64_t)%s << (64 - 0x11))) ^ (uint64_t)%s' % (val, val, pointer_guard))

def bt_jmpbuf(jmpbuf):
    '''Backtrace a jmpbuf'''
    JB_RBX  = 0
    JB_RBP  = 1
    JB_R12  = 2
    JB_R13  = 3
    JB_R14  = 4
    JB_R15  = 5
    JB_RSP  = 6
    JB_PC   = 7

    old_rbx = gdb.parse_and_eval('(uint64_t)$rbx')
    old_rbp = gdb.parse_and_eval('(uint64_t)$rbp')
    old_rsp = gdb.parse_and_eval('(uint64_t)$rsp')
    old_r12 = gdb.parse_and_eval('(uint64_t)$r12')
    old_r13 = gdb.parse_and_eval('(uint64_t)$r13')
    old_r14 = gdb.parse_and_eval('(uint64_t)$r14')
    old_r15 = gdb.parse_and_eval('(uint64_t)$r15')
    old_rip = gdb.parse_and_eval('(uint64_t)$rip')

    pointer_guard = get_glibc_pointer_guard()
    gdb.execute('set $rbx = %s' % jmpbuf[JB_RBX])
    gdb.execute('set $rbp = %s' % glibc_ptr_demangle(jmpbuf[JB_RBP], pointer_guard))
    gdb.execute('set $rsp = %s' % glibc_ptr_demangle(jmpbuf[JB_RSP], pointer_guard))
    gdb.execute('set $r12 = %s' % jmpbuf[JB_R12])
    gdb.execute('set $r13 = %s' % jmpbuf[JB_R13])
    gdb.execute('set $r14 = %s' % jmpbuf[JB_R14])
    gdb.execute('set $r15 = %s' % jmpbuf[JB_R15])
    gdb.execute('set $rip = %s' % glibc_ptr_demangle(jmpbuf[JB_PC], pointer_guard))

    gdb.execute('bt')

    gdb.execute('set $rbx = %s' % old_rbx)
    gdb.execute('set $rbp = %s' % old_rbp)
    gdb.execute('set $rsp = %s' % old_rsp)
    gdb.execute('set $r12 = %s' % old_r12)
    gdb.execute('set $r13 = %s' % old_r13)
    gdb.execute('set $r14 = %s' % old_r14)
    gdb.execute('set $r15 = %s' % old_r15)
    gdb.execute('set $rip = %s' % old_rip)

class QemuCommand(gdb.Command):
    '''Prefix for QEMU debug support commands'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE, True)

class CoroutineCommand(gdb.Command):
    '''Display coroutine backtrace'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu coroutine', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) != 1:
            gdb.write('usage: qemu coroutine <coroutine-pointer>\n')
            return

        coroutine_pointer = gdb.parse_and_eval(argv[0]).cast(gdb.lookup_type('CoroutineUContext').pointer())
        bt_jmpbuf(coroutine_pointer['env']['__jmpbuf'])

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
            if long(ptr) in self.seen:
                continue
            self.print_item(ptr)
    def print_item(self, ptr, offset = gdb.Value(0), level = 0):
        self.seen.add(long(ptr))
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
                     long(addr),
                     long(addr + (size - 1)),
                     ptr['name'].string(),
                     klass,
                     ptr,
                     ),
                  gdb.STDOUT)
        if not isnull(alias):
            gdb.write('%s    alias: %s@%016x (@ %s)\n' %
                      ('  ' * level,
                       alias['name'].string(),
                       ptr['alias_offset'],
                       alias,
                       ),
                      gdb.STDOUT)
            self.queue.append(alias)
        subregion = ptr['subregions']['tqh_first']
        level += 1
        while not isnull(subregion):
            self.print_item(subregion, addr, level)
            subregion = subregion['subregions_link']['tqe_next']

QemuCommand()
CoroutineCommand()
MtreeCommand()
