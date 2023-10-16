# Functional test that boots a Linux kernel and checks the console
#
# Copyright IBM Corp. 2023
#
# Author:
#  Pierre Morel <pmorel@linux.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import shutil
import time

from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive


class S390CPUTopology(QemuSystemTest):
    """
    S390x CPU topology consists of 4 topology layers, from bottom to top,
    the cores, sockets, books and drawers and 2 modifiers attributes,
    the entitlement and the dedication.
    See: docs/system/s390x/cpu-topology.rst.

    S390x CPU topology is setup in different ways:
    - implicitly from the '-smp' argument by completing each topology
      level one after the other beginning with drawer 0, book 0 and
      socket 0.
    - explicitly from the '-device' argument on the QEMU command line
    - explicitly by hotplug of a new CPU using QMP or HMP
    - it is modified by using QMP 'set-cpu-topology'

    The S390x modifier attribute entitlement depends on the machine
    polarization, which can be horizontal or vertical.
    The polarization is changed on a request from the guest.
    """
    timeout = 90

    KERNEL_COMMON_COMMAND_LINE = ('printk.time=0 '
                                  'root=/dev/ram '
                                  'selinux=0 '
                                  'rdinit=/bin/sh')

    def wait_until_booted(self):
        wait_for_console_pattern(self, 'no job control',
                                 failure_message='Kernel panic - not syncing',
                                 vm=None)

    def check_topology(self, c, s, b, d, e, t):
        res = self.vm.qmp('query-cpus-fast')
        cpus =  res['return']
        for cpu in cpus:
            core = cpu['props']['core-id']
            socket = cpu['props']['socket-id']
            book = cpu['props']['book-id']
            drawer = cpu['props']['drawer-id']
            entitlement = cpu.get('entitlement')
            dedicated = cpu.get('dedicated')
            if core == c:
                self.assertEqual(drawer, d)
                self.assertEqual(book, b)
                self.assertEqual(socket, s)
                self.assertEqual(entitlement, e)
                self.assertEqual(dedicated, t)

    def kernel_init(self):
        """
        We need a VM that supports CPU topology,
        currently this only the case when using KVM, not TCG.
        We need a kernel supporting the CPU topology.
        We need a minimal root filesystem with a shell.
        """
        self.require_accelerator("kvm")
        kernel_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/35/Server/s390x/os'
                      '/images/kernel.img')
        kernel_hash = '0d1aaaf303f07cf0160c8c48e56fe638'
        kernel_path = self.fetch_asset(kernel_url, algorithm='md5',
                                       asset_hash=kernel_hash)

        initrd_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/35/Server/s390x/os'
                      '/images/initrd.img')
        initrd_hash = 'a122057d95725ac030e2ec51df46e172'
        initrd_path_xz = self.fetch_asset(initrd_url, algorithm='md5',
                                          asset_hash=initrd_hash)
        initrd_path = os.path.join(self.workdir, 'initrd-raw.img')
        archive.lzma_uncompress(initrd_path_xz, initrd_path)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args('-nographic',
                         '-enable-kvm',
                         '-cpu', 'max,ctop=on',
                         '-m', '512',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line)

    def test_single(self):
        """
        This test checks the simplest topology with a single CPU.

        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """
        self.kernel_init()
        self.vm.launch()
        self.wait_until_booted()
        self.check_topology(0, 0, 0, 0, 'medium', False)

    def test_default(self):
        """
        This test checks the implicit topology.

        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """
        self.kernel_init()
        self.vm.add_args('-smp',
                         '13,drawers=2,books=2,sockets=3,cores=2,maxcpus=24')
        self.vm.launch()
        self.wait_until_booted()
        self.check_topology(0, 0, 0, 0, 'medium', False)
        self.check_topology(1, 0, 0, 0, 'medium', False)
        self.check_topology(2, 1, 0, 0, 'medium', False)
        self.check_topology(3, 1, 0, 0, 'medium', False)
        self.check_topology(4, 2, 0, 0, 'medium', False)
        self.check_topology(5, 2, 0, 0, 'medium', False)
        self.check_topology(6, 0, 1, 0, 'medium', False)
        self.check_topology(7, 0, 1, 0, 'medium', False)
        self.check_topology(8, 1, 1, 0, 'medium', False)
        self.check_topology(9, 1, 1, 0, 'medium', False)
        self.check_topology(10, 2, 1, 0, 'medium', False)
        self.check_topology(11, 2, 1, 0, 'medium', False)
        self.check_topology(12, 0, 0, 1, 'medium', False)

    def test_move(self):
        """
        This test checks the topology modification by moving a CPU
        to another socket: CPU 0 is moved from socket 0 to socket 2.

        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """
        self.kernel_init()
        self.vm.add_args('-smp',
                         '1,drawers=2,books=2,sockets=3,cores=2,maxcpus=24')
        self.vm.launch()
        self.wait_until_booted()

        self.check_topology(0, 0, 0, 0, 'medium', False)
        res = self.vm.qmp('set-cpu-topology',
                          {'core-id': 0, 'socket-id': 2, 'entitlement': 'low'})
        self.assertEqual(res['return'], {})
        self.check_topology(0, 2, 0, 0, 'low', False)

    def test_dash_device(self):
        """
        This test verifies that a CPU defined with the '-device'
        command line option finds its right place inside the topology.

        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """
        self.kernel_init()
        self.vm.add_args('-smp',
                         '1,drawers=2,books=2,sockets=3,cores=2,maxcpus=24')
        self.vm.add_args('-device', 'max-s390x-cpu,core-id=10')
        self.vm.add_args('-device',
                         'max-s390x-cpu,'
                         'core-id=1,socket-id=0,book-id=1,drawer-id=1,entitlement=low')
        self.vm.add_args('-device',
                         'max-s390x-cpu,'
                         'core-id=2,socket-id=0,book-id=1,drawer-id=1,entitlement=medium')
        self.vm.add_args('-device',
                         'max-s390x-cpu,'
                         'core-id=3,socket-id=1,book-id=1,drawer-id=1,entitlement=high')
        self.vm.add_args('-device',
                         'max-s390x-cpu,'
                         'core-id=4,socket-id=1,book-id=1,drawer-id=1')
        self.vm.add_args('-device',
                         'max-s390x-cpu,'
                         'core-id=5,socket-id=2,book-id=1,drawer-id=1,dedicated=true')

        self.vm.launch()
        self.wait_until_booted()

        self.check_topology(10, 2, 1, 0, 'medium', False)
        self.check_topology(1, 0, 1, 1, 'low', False)
        self.check_topology(2, 0, 1, 1, 'medium', False)
        self.check_topology(3, 1, 1, 1, 'high', False)
        self.check_topology(4, 1, 1, 1, 'medium', False)
        self.check_topology(5, 2, 1, 1, 'high', True)
