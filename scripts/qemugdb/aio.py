#!/usr/bin/python

# GDB debugging support: aio/iohandler debug
#
# Copyright (c) 2015 Red Hat, Inc.
#
# Author: Dr. David Alan Gilbert <dgilbert@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

import gdb
from qemugdb import coroutine

def isnull(ptr):
    return ptr == gdb.Value(0).cast(ptr.type)

def dump_aiocontext(context, verbose):
    '''Display a dump and backtrace for an aiocontext'''
    cur = context['aio_handlers']['lh_first']
    # Get pointers to functions we're going to process specially
    sym_fd_coroutine_enter = gdb.parse_and_eval('fd_coroutine_enter')

    while not isnull(cur):
        entry = cur.dereference()
        gdb.write('----\n%s\n' % entry)
        if verbose and cur['io_read'] == sym_fd_coroutine_enter:
            coptr = (cur['opaque'].cast(gdb.lookup_type('FDYieldUntilData').pointer()))['co']
            coptr = coptr.cast(gdb.lookup_type('CoroutineUContext').pointer())
            coroutine.bt_jmpbuf(coptr['env']['__jmpbuf'])
        cur = cur['node']['le_next'];

    gdb.write('----\n')

class HandlersCommand(gdb.Command):
    '''Display aio handlers'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu handlers', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        verbose = False
        argv = gdb.string_to_argv(arg)

        if len(argv) > 0 and argv[0] == '--verbose':
            verbose = True
            argv.pop(0)

        if len(argv) > 1:
            gdb.write('usage: qemu handlers [--verbose] [handler]\n')
            return

        if len(argv) == 1:
            handlers_name = argv[0]
        else:
            handlers_name = 'qemu_aio_context'
        dump_aiocontext(gdb.parse_and_eval(handlers_name), verbose)
