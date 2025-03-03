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
import logging
import time
import subprocess

from qemu_test.linuxkernel import LinuxKernelTest

class ReplayKernelBase(LinuxKernelTest):
    """
    Boots a Linux kernel in record mode and checks that the console
    is operational and the kernel command line is properly passed
    from QEMU to the kernel.
    Then replays the same scenario and verifies, that QEMU correctly
    terminates.
    """

    timeout = 180
    REPLAY_KERNEL_COMMAND_LINE = 'printk.time=1 panic=-1 '

    def run_vm(self, kernel_path, kernel_command_line, console_pattern,
               record, shift, args, replay_path):
        # icount requires TCG to be available
        self.require_accelerator('tcg')

        logger = logging.getLogger('replay')
        start_time = time.time()
        vm = self.get_vm(name='recording' if record else 'replay')
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
