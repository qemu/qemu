# Record/replay test that boots a complete Linux system via a cloud image
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

from avocado import skipUnless
from avocado.utils import cloudinit
from avocado.utils import network
from avocado.utils import vmimage
from avocado.utils import datadrainer
from avocado.utils.path import find_command
from avocado_qemu import LinuxTest

class ReplayLinux(LinuxTest):
    """
    Boots a Linux system, checking for a successful initialization
    """

    timeout = 1800
    chksum = None
    hdd = 'ide-hd'
    cd = 'ide-cd'
    bus = 'ide'

    def setUp(self):
        super(ReplayLinux, self).setUp()
        self.boot_path = self.download_boot()
        self.cloudinit_path = self.prepare_cloudinit()

    def vm_add_disk(self, vm, path, id, device):
        bus_string = ''
        if self.bus:
            bus_string = ',bus=%s.%d' % (self.bus, id,)
        vm.add_args('-drive', 'file=%s,snapshot,id=disk%s,if=none' % (path, id))
        vm.add_args('-drive',
            'driver=blkreplay,id=disk%s-rr,if=none,image=disk%s' % (id, id))
        vm.add_args('-device',
            '%s,drive=disk%s-rr%s' % (device, id, bus_string))

    def launch_and_wait(self, record, args, shift):
        vm = self.get_vm()
        vm.add_args('-smp', '1')
        vm.add_args('-m', '1024')
        vm.add_args('-object', 'filter-replay,id=replay,netdev=hub0port0')
        if args:
            vm.add_args(*args)
        self.vm_add_disk(vm, self.boot_path, 0, self.hdd)
        self.vm_add_disk(vm, self.cloudinit_path, 1, self.cd)
        logger = logging.getLogger('replay')
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
        replay_path = os.path.join(self.workdir, 'replay.bin')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, replay_path))

        start_time = time.time()

        vm.set_console()
        vm.launch()
        console_drainer = datadrainer.LineLogger(vm.console_socket.fileno(),
                                    logger=self.log.getChild('console'),
                                    stop_check=(lambda : not vm.is_running()))
        console_drainer.start()
        if record:
            cloudinit.wait_for_phone_home(('0.0.0.0', self.phone_home_port),
                                          self.name)
            vm.shutdown()
            logger.info('finished the recording with log size %s bytes'
                % os.path.getsize(replay_path))
        else:
            vm.event_wait('SHUTDOWN', self.timeout)
            vm.shutdown(True)
            logger.info('successfully fihished the replay')
        elapsed = time.time() - start_time
        logger.info('elapsed time %.2f sec' % elapsed)
        return elapsed

    def run_rr(self, args=None, shift=7):
        t1 = self.launch_and_wait(True, args, shift)
        t2 = self.launch_and_wait(False, args, shift)
        logger = logging.getLogger('replay')
        logger.info('replay overhead {:.2%}'.format(t2 / t1 - 1))

@skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
class ReplayLinuxX8664(ReplayLinux):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=accel:tcg
    """

    chksum = 'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0'

    def test_pc_i440fx(self):
        """
        :avocado: tags=machine:pc
        """
        self.run_rr(shift=1)

    def test_pc_q35(self):
        """
        :avocado: tags=machine:q35
        """
        self.run_rr(shift=3)
