# Reverse debugging test
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import os
import logging

from avocado import skipUnless
from avocado_qemu import BUILD_DIR
from avocado.utils import datadrainer
from avocado.utils import gdb
from avocado.utils import process
from avocado.utils.network.ports import find_free_port
from avocado.utils.path import find_command
from boot_linux_console import LinuxKernelTest

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

    timeout = 10
    STEPS = 10
    endian_is_le = True

    def run_vm(self, record, shift, args, replay_path, image_path, port):
        logger = logging.getLogger('replay')
        vm = self.get_vm()
        vm.set_console()
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
            vm.add_args('-gdb', 'tcp::%d' % port, '-S')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s,rrsnapshot=init' %
                    (shift, mode, replay_path),
                    '-net', 'none')
        vm.add_args('-drive', 'file=%s,if=none' % image_path)
        if args:
            vm.add_args(*args)
        vm.launch()
        console_drainer = datadrainer.LineLogger(vm.console_socket.fileno(),
                                    logger=self.log.getChild('console'),
                                    stop_check=(lambda : not vm.is_running()))
        console_drainer.start()
        return vm

    @staticmethod
    def get_reg_le(g, reg):
        res = g.cmd(b'p%x' % reg)
        num = 0
        for i in range(len(res))[-2::-2]:
            num = 0x100 * num + int(res[i:i + 2], 16)
        return num

    @staticmethod
    def get_reg_be(g, reg):
        res = g.cmd(b'p%x' % reg)
        return int(res, 16)

    def get_reg(self, g, reg):
        # value may be encoded in BE or LE order
        if self.endian_is_le:
            return self.get_reg_le(g, reg)
        else:
            return self.get_reg_be(g, reg)

    def get_pc(self, g):
        return self.get_reg(g, self.REG_PC)

    def check_pc(self, g, addr):
        pc = self.get_pc(g)
        if pc != addr:
            self.fail('Invalid PC (read %x instead of %x)' % (pc, addr))

    @staticmethod
    def gdb_step(g):
        g.cmd(b's', b'T05thread:01;')

    @staticmethod
    def gdb_bstep(g):
        g.cmd(b'bs', b'T05thread:01;')

    @staticmethod
    def vm_get_icount(vm):
        return vm.qmp('query-replay')['return']['icount']

    def reverse_debugging(self, shift=7, args=None):
        logger = logging.getLogger('replay')

        # create qcow2 for snapshots
        logger.info('creating qcow2 image for VM snapshots')
        image_path = os.path.join(self.workdir, 'disk.qcow2')
        qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
        if not os.path.exists(qemu_img):
            qemu_img = find_command('qemu-img', False)
        if qemu_img is False:
            self.cancel('Could not find "qemu-img", which is required to '
                        'create the temporary qcow2 image')
        cmd = '%s create -f qcow2 %s 128M' % (qemu_img, image_path)
        process.run(cmd)

        replay_path = os.path.join(self.workdir, 'replay.bin')
        port = find_free_port()

        # record the log
        vm = self.run_vm(True, shift, args, replay_path, image_path, port)
        while self.vm_get_icount(vm) <= self.STEPS:
            pass
        last_icount = self.vm_get_icount(vm)
        vm.shutdown()

        logger.info("recorded log with %s+ steps" % last_icount)

        # replay and run debug commands
        vm = self.run_vm(False, shift, args, replay_path, image_path, port)
        logger.info('connecting to gdbstub')
        g = gdb.GDBRemote('127.0.0.1', port, False, False)
        g.connect()
        r = g.cmd(b'qSupported')
        if b'qXfer:features:read+' in r:
            g.cmd(b'qXfer:features:read:target.xml:0,ffb')
        if b'ReverseStep+' not in r:
            self.fail('Reverse step is not supported by QEMU')
        if b'ReverseContinue+' not in r:
            self.fail('Reverse continue is not supported by QEMU')

        logger.info('stepping forward')
        steps = []
        # record first instruction addresses
        for _ in range(self.STEPS):
            pc = self.get_pc(g)
            logger.info('saving position %x' % pc)
            steps.append(pc)
            self.gdb_step(g)

        # visit the recorded instruction in reverse order
        logger.info('stepping backward')
        for addr in steps[::-1]:
            self.gdb_bstep(g)
            self.check_pc(g, addr)
            logger.info('found position %x' % addr)

        # visit the recorded instruction in forward order
        logger.info('stepping forward')
        for addr in steps:
            self.check_pc(g, addr)
            self.gdb_step(g)
            logger.info('found position %x' % addr)

        # set breakpoints for the instructions just stepped over
        logger.info('setting breakpoints')
        for addr in steps:
            # hardware breakpoint at addr with len=1
            g.cmd(b'Z1,%x,1' % addr, b'OK')

        # this may hit a breakpoint if first instructions are executed
        # again
        logger.info('continuing execution')
        vm.qmp('replay-break', icount=last_icount - 1)
        # continue - will return after pausing
        # This could stop at the end and get a T02 return, or by
        # re-executing one of the breakpoints and get a T05 return.
        g.cmd(b'c')
        if self.vm_get_icount(vm) == last_icount - 1:
            logger.info('reached the end (icount %s)' % (last_icount - 1))
        else:
            logger.info('hit a breakpoint again at %x (icount %s)' %
                        (self.get_pc(g), self.vm_get_icount(vm)))

        logger.info('running reverse continue to reach %x' % steps[-1])
        # reverse continue - will return after stopping at the breakpoint
        g.cmd(b'bc', b'T05thread:01;')

        # assume that none of the first instructions is executed again
        # breaking the order of the breakpoints
        self.check_pc(g, steps[-1])
        logger.info('successfully reached %x' % steps[-1])

        logger.info('exitting gdb and qemu')
        vm.shutdown()

class ReverseDebugging_X86_64(ReverseDebugging):
    """
    :avocado: tags=accel:tcg
    """

    REG_PC = 0x10
    REG_CS = 0x12
    def get_pc(self, g):
        return self.get_reg_le(g, self.REG_PC) \
            + self.get_reg_le(g, self.REG_CS) * 0x10

    # unidentified gitlab timeout problem
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        # start with BIOS only
        self.reverse_debugging()

class ReverseDebugging_AArch64(ReverseDebugging):
    """
    :avocado: tags=accel:tcg
    """

    REG_PC = 32

    # unidentified gitlab timeout problem
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_aarch64_virt(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:cortex-a53
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/aarch64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '8c73e469fc6ea06a58dc83a628fc695b693b8493'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.reverse_debugging(
            args=('-kernel', kernel_path))

class ReverseDebugging_ppc64(ReverseDebugging):
    """
    :avocado: tags=accel:tcg
    """

    REG_PC = 0x40

    # unidentified gitlab timeout problem
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_ppc64_pseries(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=flaky
        """
        # SLOF branches back to its entry point, which causes this test
        # to take the 'hit a breakpoint again' path. That's not a problem,
        # just slightly different than the other machines.
        self.endian_is_le = False
        self.reverse_debugging()

    # See https://gitlab.com/qemu-project/qemu/-/issues/1992
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_ppc64_powernv(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=flaky
        """
        self.endian_is_le = False
        self.reverse_debugging()
