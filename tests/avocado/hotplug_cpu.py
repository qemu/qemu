# Functional test that hotplugs a CPU and checks it on a Linux guest
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import LinuxTest


class HotPlugCPU(LinuxTest):

    def test(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:q35
        :avocado: tags=accel:kvm
        """
        self.require_accelerator('kvm')
        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-cpu', 'Haswell')
        self.vm.add_args('-smp', '1,sockets=1,cores=2,threads=1,maxcpus=2')
        self.launch_and_wait()

        self.ssh_command('test -e /sys/devices/system/cpu/cpu0')
        with self.assertRaises(AssertionError):
            self.ssh_command('test -e /sys/devices/system/cpu/cpu1')

        self.vm.command('device_add',
                        driver='Haswell-x86_64-cpu',
                        socket_id=0,
                        core_id=1,
                        thread_id=0)
        self.ssh_command('test -e /sys/devices/system/cpu/cpu1')
