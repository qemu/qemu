# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test for multiprocess qemu
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


import os
import socket

from qemu_test import QemuSystemTest, wait_for_console_pattern
from qemu_test import exec_command, exec_command_and_wait_for_pattern

class Multiprocess(QemuSystemTest):

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def do_test(self, kernel_asset, initrd_asset,
                kernel_command_line, machine_type):
        """Main test method"""
        self.require_accelerator('kvm')
        self.require_device('x-pci-proxy-dev')

        # Create socketpair to connect proxy and remote processes
        proxy_sock, remote_sock = socket.socketpair(socket.AF_UNIX,
                                                    socket.SOCK_STREAM)
        os.set_inheritable(proxy_sock.fileno(), True)
        os.set_inheritable(remote_sock.fileno(), True)

        kernel_path = kernel_asset.fetch()
        initrd_path = initrd_asset.fetch()

        # Create remote process
        remote_vm = self.get_vm()
        remote_vm.add_args('-machine', 'x-remote')
        remote_vm.add_args('-nodefaults')
        remote_vm.add_args('-device', 'lsi53c895a,id=lsi1')
        remote_vm.add_args('-object', 'x-remote-object,id=robj1,'
                           'devid=lsi1,fd='+str(remote_sock.fileno()))
        remote_vm.launch()

        # Create proxy process
        self.vm.set_console()
        self.vm.add_args('-machine', machine_type)
        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-cpu', 'host')
        self.vm.add_args('-object',
                         'memory-backend-memfd,id=sysmem-file,size=2G')
        self.vm.add_args('--numa', 'node,memdev=sysmem-file')
        self.vm.add_args('-m', '2048')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line)
        self.vm.add_args('-device',
                         'x-pci-proxy-dev,'
                         'id=lsi1,fd='+str(proxy_sock.fileno()))
        self.vm.launch()
        wait_for_console_pattern(self, 'as init process',
                                 'Kernel panic - not syncing')
        exec_command(self, 'mount -t sysfs sysfs /sys')
        exec_command_and_wait_for_pattern(self,
                                          'cat /sys/bus/pci/devices/*/uevent',
                                          'PCI_ID=1000:0012')

        proxy_sock.close()
        remote_sock.close()
