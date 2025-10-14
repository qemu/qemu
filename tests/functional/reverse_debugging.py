# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test
#
# Copyright (c) 2020 ISP RAS
# Copyright (c) 2025 Linaro Limited
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#  Gustavo Romero <gustavo.romero@linaro.org> (Run without Avocado)
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import os
from subprocess import check_output

from qemu_test import LinuxKernelTest, get_qemu_img, GDB, \
    skipIfMissingEnv, skipIfMissingImports
from qemu_test.ports import Ports


class ReverseDebugging(LinuxKernelTest):
    """
    Test GDB reverse debugging commands: reverse step and reverse continue.
    Recording saves the execution of some instructions and makes an initial
    VM snapshot to allow reverse execution.
    Replay saves the order of the first instructions and then checks that they
    are executed backwards in the correct order.
    After that the execution is replayed to the end, and reverse continue
    command is checked by setting several breakpoints, and asserting
    that the execution is stopped at the last of them.
    """

    STEPS = 10

    def run_vm(self, record, shift, args, replay_path, image_path, port):
        vm = self.get_vm(name='record' if record else 'replay')
        vm.set_console()
        if record:
            self.log.info('recording the execution...')
            mode = 'record'
        else:
            self.log.info('replaying the execution...')
            mode = 'replay'
            vm.add_args('-gdb', 'tcp::%d' % port, '-S')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s,rrsnapshot=init' %
                    (shift, mode, replay_path),
                    '-net', 'none')
        vm.add_args('-drive', 'file=%s,if=none' % image_path)
        if args:
            vm.add_args(*args)
        vm.launch()
        return vm

    @staticmethod
    def get_pc(gdb: GDB):
        return gdb.cli("print $pc").get_addr()

    @staticmethod
    def vm_get_icount(vm):
        return vm.qmp('query-replay')['return']['icount']

    @skipIfMissingImports("pygdbmi") # Required by GDB class
    @skipIfMissingEnv("QEMU_TEST_GDB")
    def reverse_debugging(self, gdb_arch, shift=7, args=None):
        from qemu_test import GDB

        # create qcow2 for snapshots
        self.log.info('creating qcow2 image for VM snapshots')
        image_path = os.path.join(self.workdir, 'disk.qcow2')
        qemu_img = get_qemu_img(self)
        if qemu_img is None:
            self.skipTest('Could not find "qemu-img", which is required to '
                          'create the temporary qcow2 image')
        out = check_output([qemu_img, 'create', '-f', 'qcow2', image_path, '128M'],
                           encoding='utf8')
        self.log.info("qemu-img: %s" % out)

        replay_path = os.path.join(self.workdir, 'replay.bin')

        # record the log
        vm = self.run_vm(True, shift, args, replay_path, image_path, -1)
        while self.vm_get_icount(vm) <= self.STEPS:
            pass
        last_icount = self.vm_get_icount(vm)
        vm.shutdown()

        self.log.info("recorded log with %s+ steps" % last_icount)

        # replay and run debug commands
        with Ports() as ports:
            port = ports.find_free_port()
            vm = self.run_vm(False, shift, args, replay_path, image_path, port)

        try:
            self.log.info('Connecting to gdbstub...')
            gdb_cmd = os.getenv('QEMU_TEST_GDB')
            gdb = GDB(gdb_cmd)
            try:
                self.reverse_debugging_run(gdb, vm, port, gdb_arch, last_icount)
            finally:
                self.log.info('exiting gdb and qemu')
                gdb.exit()
                vm.shutdown()
            self.log.info('Test passed.')
        except GDB.TimeoutError:
            # Convert a GDB timeout exception into a unittest failure exception.
            raise self.failureException("Timeout while connecting to or "
                                        "communicating with gdbstub...") from None
        except Exception:
            # Re-throw exceptions from unittest, like the ones caused by fail(),
            # skipTest(), etc.
            raise

    def reverse_debugging_run(self, gdb, vm, port, gdb_arch, last_icount):
        r = gdb.cli("set architecture").get_log()
        if gdb_arch not in r:
            self.skipTest(f"GDB does not support arch '{gdb_arch}'")

        gdb.cli("set debug remote 1")

        c = gdb.cli(f"target remote localhost:{port}").get_console()
        if not f"Remote debugging using localhost:{port}" in c:
            self.fail("Could not connect to gdbstub!")

        # Remote debug messages are in 'log' payloads.
        r = gdb.get_log()
        if 'ReverseStep+' not in r:
            self.fail('Reverse step is not supported by QEMU')
        if 'ReverseContinue+' not in r:
            self.fail('Reverse continue is not supported by QEMU')

        gdb.cli("set debug remote 0")

        self.log.info('stepping forward')
        steps = []
        # record first instruction addresses
        for _ in range(self.STEPS):
            pc = self.get_pc(gdb)
            self.log.info('saving position %x' % pc)
            steps.append(pc)
            gdb.cli("stepi")

        # visit the recorded instruction in reverse order
        self.log.info('stepping backward')
        for addr in steps[::-1]:
            self.log.info('found position %x' % addr)
            gdb.cli("reverse-stepi")
            pc = self.get_pc(gdb)
            if pc != addr:
                self.log.info('Invalid PC (read %x instead of %x)' % (pc, addr))
                self.fail('Reverse stepping failed!')

        # visit the recorded instruction in forward order
        self.log.info('stepping forward')
        for addr in steps:
            self.log.info('found position %x' % addr)
            pc = self.get_pc(gdb)
            if pc != addr:
                self.log.info('Invalid PC (read %x instead of %x)' % (pc, addr))
                self.fail('Forward stepping failed!')
            gdb.cli("stepi")

        # set breakpoints for the instructions just stepped over
        self.log.info('setting breakpoints')
        for addr in steps:
            gdb.cli(f"break *{hex(addr)}")

        # this may hit a breakpoint if first instructions are executed
        # again
        self.log.info('continuing execution')
        vm.qmp('replay-break', icount=last_icount - 1)
        # continue - will return after pausing
        # This can stop at the end of the replay-break and gdb gets a SIGINT,
        # or by re-executing one of the breakpoints and gdb stops at a
        # breakpoint.
        gdb.cli("continue")

        if self.vm_get_icount(vm) == last_icount - 1:
            self.log.info('reached the end (icount %s)' % (last_icount - 1))
        else:
            self.log.info('hit a breakpoint again at %x (icount %s)' %
                        (self.get_pc(gdb), self.vm_get_icount(vm)))

        self.log.info('running reverse continue to reach %x' % steps[-1])
        # reverse continue - will return after stopping at the breakpoint
        gdb.cli("reverse-continue")

        # assume that none of the first instructions is executed again
        # breaking the order of the breakpoints
        pc = self.get_pc(gdb)
        if pc != steps[-1]:
            self.fail("'reverse-continue' did not hit the first PC in reverse order!")

        self.log.info('successfully reached %x' % steps[-1])
