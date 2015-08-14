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
