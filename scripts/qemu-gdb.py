#!/usr/bin/env python3
#
# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

# Usage:
# At the (gdb) prompt, type "source scripts/qemu-gdb.py".
# "help qemu" should then list the supported QEMU debug support commands.

import gdb

import os, sys

# Annoyingly, gdb doesn't put the directory of scripts onto the
# module search path. Do it manually.

sys.path.append(os.path.dirname(__file__))

from qemugdb import aio, mtree, coroutine, tcg, timers

class QemuCommand(gdb.Command):
    '''Prefix for QEMU debug support commands'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE, True)

QemuCommand()
coroutine.CoroutineCommand()
mtree.MtreeCommand()
aio.HandlersCommand()
tcg.TCGLockStatusCommand()
timers.TimersCommand()

coroutine.CoroutineSPFunction()
coroutine.CoroutinePCFunction()

# Default to silently passing through SIGUSR1, because QEMU sends it
# to itself a lot.
gdb.execute('handle SIGUSR1 pass noprint nostop')
