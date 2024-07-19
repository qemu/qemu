# Functional test that hotplugs a virtio blk disk and checks it on a Linux
# guest
#
# Copyright (c) 2021 Red Hat, Inc.
# Copyright (c) Yandex
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time

from avocado_qemu.linuxtest import LinuxTest


class HotPlug(LinuxTest):
    def blockdev_add(self) -> None:
        self.vm.cmd('blockdev-add', **{
            'driver': 'null-co',
            'size': 1073741824,
            'node-name': 'disk'
        })

    def assert_vda(self) -> None:
        self.ssh_command('test -e /sys/block/vda')

    def assert_no_vda(self) -> None:
        with self.assertRaises(AssertionError):
            self.assert_vda()

    def plug(self) -> None:
        args = {
            'driver': 'virtio-blk-pci',
            'drive': 'disk',
            'id': 'virtio-disk0',
            'bus': 'pci.1',
            'addr': 1
        }

        self.assert_no_vda()
        self.vm.cmd('device_add', args)
        try:
            self.assert_vda()
        except AssertionError:
            time.sleep(1)
            self.assert_vda()

    def unplug(self) -> None:
        self.vm.cmd('device_del', id='virtio-disk0')

        self.vm.event_wait('DEVICE_DELETED', 1.0,
                           match={'data': {'device': 'virtio-disk0'}})

        self.assert_no_vda()

    def test(self) -> None:
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:q35
        :avocado: tags=accel:kvm
        """
        self.require_accelerator('kvm')
        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-device', 'pcie-pci-bridge,id=pci.1,bus=pcie.0')

        self.launch_and_wait()
        self.blockdev_add()

        self.plug()
        self.unplug()
