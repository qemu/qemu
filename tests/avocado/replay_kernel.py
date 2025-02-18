# Record/replay test that boots a Linux kernel
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgaluk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import lzma
import shutil
import logging
import time
import subprocess

from avocado import skip
from avocado import skipUnless
from avocado import skipUnless
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive
from avocado.utils import process
from boot_linux_console import LinuxKernelTest

class ReplayKernelBase(LinuxKernelTest):
    """
    Boots a Linux kernel in record mode and checks that the console
    is operational and the kernel command line is properly passed
    from QEMU to the kernel.
    Then replays the same scenario and verifies, that QEMU correctly
    terminates.
    """

    timeout = 180
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=1 panic=-1 '

    def run_vm(self, kernel_path, kernel_command_line, console_pattern,
               record, shift, args, replay_path):
        # icount requires TCG to be available
        self.require_accelerator('tcg')

        logger = logging.getLogger('replay')
        start_time = time.time()
        vm = self.get_vm()
        vm.set_console()
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, replay_path),
                    '-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-net', 'none',
                    '-no-reboot')
        if args:
            vm.add_args(*args)
        vm.launch()
        self.wait_for_console_pattern(console_pattern, vm)
        if record:
            vm.shutdown()
            logger.info('finished the recording with log size %s bytes'
                        % os.path.getsize(replay_path))
            self.run_replay_dump(replay_path)
            logger.info('successfully tested replay-dump.py')
        else:
            vm.wait()
            logger.info('successfully finished the replay')
        elapsed = time.time() - start_time
        logger.info('elapsed time %.2f sec' % elapsed)
        return elapsed

    def run_replay_dump(self, replay_path):
        try:
            subprocess.check_call(["./scripts/replay-dump.py",
                                   "-f", replay_path],
                                  stdout=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            self.fail('replay-dump.py failed')

    def run_rr(self, kernel_path, kernel_command_line, console_pattern,
               shift=7, args=None):
        replay_path = os.path.join(self.workdir, 'replay.bin')
        t1 = self.run_vm(kernel_path, kernel_command_line, console_pattern,
                         True, shift, args, replay_path)
        t2 = self.run_vm(kernel_path, kernel_command_line, console_pattern,
                         False, shift, args, replay_path)
        logger = logging.getLogger('replay')
        logger.info('replay overhead {:.2%}'.format(t2 / t1 - 1))

class ReplayKernelNormal(ReplayKernelBase):

    def test_i386_pc(self):
        """
        :avocado: tags=arch:i386
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://storage.tuxboot.com/20230331/i386/bzImage')
        kernel_hash = 'a3e5b32a354729e65910f5a1ffcda7c14a6c12a55e8213fb86e277f1b76ed956'
        kernel_path = self.fetch_asset(kernel_url,
                                       asset_hash=kernel_hash,
                                       algorithm = "sha256")

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'VFS: Cannot open root device'

        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)
