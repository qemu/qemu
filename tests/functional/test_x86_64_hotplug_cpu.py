#!/usr/bin/env python3
#
# Functional test that hotplugs a CPU and checks it on a Linux guest
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern


class HotPlugCPU(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/initrd.img'),
        '277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b')

    def test_hotplug(self):

        self.require_accelerator('kvm')
        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-cpu', 'Haswell')
        self.vm.add_args('-smp', '1,sockets=1,cores=2,threads=1,maxcpus=2')
        self.vm.add_args('-m', '1G')
        self.vm.add_args('-append', 'console=ttyS0 rd.rescue')

        self.launch_kernel(self.ASSET_KERNEL.fetch(),
                           self.ASSET_INITRD.fetch(),
                           wait_for='Entering emergency mode.')
        prompt = '# '
        self.wait_for_console_pattern(prompt)

        exec_command_and_wait_for_pattern(self,
                                          'cd /sys/devices/system/cpu/cpu0',
                                          'cpu0#')
        exec_command_and_wait_for_pattern(self,
                                          'cd /sys/devices/system/cpu/cpu1',
                                          'No such file or directory')

        self.vm.cmd('device_add',
                    driver='Haswell-x86_64-cpu',
                    id='c1',
                    socket_id=0,
                    core_id=1,
                    thread_id=0)
        self.wait_for_console_pattern('CPU1 has been hot-added')

        exec_command_and_wait_for_pattern(self,
                                          'cd /sys/devices/system/cpu/cpu1',
                                          'cpu1#')

        exec_command_and_wait_for_pattern(self, 'cd ..', prompt)
        self.vm.cmd('device_del', id='c1')

        exec_command_and_wait_for_pattern(self,
                                    'while cd /sys/devices/system/cpu/cpu1 ;'
                                    ' do sleep 0.2 ; done',
                                    'No such file or directory')

if __name__ == '__main__':
    LinuxKernelTest.main()
