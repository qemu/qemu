#!/usr/bin/python

# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or later.  See the COPYING file in the top-level directory.

import gdb

VOID_PTR = gdb.lookup_type('void').pointer()

def get_fs_base():
    '''Fetch %fs base value using arch_prctl(ARCH_GET_FS).  This is
       pthread_self().'''
    # %rsp - 120 is scratch space according to the SystemV ABI
    old = gdb.parse_and_eval('*(uint64_t*)($rsp - 120)')
    gdb.execute('call (int)arch_prctl(0x1003, $rsp - 120)', False, True)
    fs_base = gdb.parse_and_eval('*(uint64_t*)($rsp - 120)')
    gdb.execute('set *(uint64_t*)($rsp - 120) = %s' % old, False, True)
    return fs_base

def pthread_self():
    '''Fetch pthread_self() from the glibc start_thread function.'''
    f = gdb.newest_frame()
    while f.name() != 'start_thread':
        f = f.older()
        if f is None:
            return get_fs_base()

    try:
        return f.read_var("arg")
    except ValueError:
        return get_fs_base()

def get_glibc_pointer_guard():
    '''Fetch glibc pointer guard value'''
    fs_base = pthread_self()
    return gdb.parse_and_eval('*(uint64_t*)((uint64_t)%s + 0x30)' % fs_base)

def glibc_ptr_demangle(val, pointer_guard):
    '''Undo effect of glibc's PTR_MANGLE()'''
    return gdb.parse_and_eval('(((uint64_t)%s >> 0x11) | ((uint64_t)%s << (64 - 0x11))) ^ (uint64_t)%s' % (val, val, pointer_guard))

def get_jmpbuf_regs(jmpbuf):
    JB_RBX  = 0
    JB_RBP  = 1
    JB_R12  = 2
    JB_R13  = 3
    JB_R14  = 4
    JB_R15  = 5
    JB_RSP  = 6
    JB_PC   = 7

    pointer_guard = get_glibc_pointer_guard()
    return {'rbx': jmpbuf[JB_RBX],
        'rbp': glibc_ptr_demangle(jmpbuf[JB_RBP], pointer_guard),
        'rsp': glibc_ptr_demangle(jmpbuf[JB_RSP], pointer_guard),
        'r12': jmpbuf[JB_R12],
        'r13': jmpbuf[JB_R13],
        'r14': jmpbuf[JB_R14],
        'r15': jmpbuf[JB_R15],
        'rip': glibc_ptr_demangle(jmpbuf[JB_PC], pointer_guard) }

def bt_jmpbuf(jmpbuf):
    '''Backtrace a jmpbuf'''
    regs = get_jmpbuf_regs(jmpbuf)
    old = dict()

    for i in regs:
        old[i] = gdb.parse_and_eval('(uint64_t)$%s' % i)

    for i in regs:
        gdb.execute('set $%s = %s' % (i, regs[i]))

    gdb.execute('bt')

    for i in regs:
        gdb.execute('set $%s = %s' % (i, old[i]))

def coroutine_to_jmpbuf(co):
    coroutine_pointer = co.cast(gdb.lookup_type('CoroutineUContext').pointer())
    return coroutine_pointer['env']['__jmpbuf']


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

        bt_jmpbuf(coroutine_to_jmpbuf(gdb.parse_and_eval(argv[0])))

class CoroutineSPFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_sp')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rsp'].cast(VOID_PTR)

class CoroutinePCFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_pc')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rip'].cast(VOID_PTR)
