#!/usr/bin/env python3
#
# Check for vm-launch-update device.
#
# Copyright (c) 2026 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest

class VmLaunchUpdateDeviceCheck(QemuSystemTest):

    def aarch64_fail_test(self):
        """
        Currently the device is only supported for pc platforms.
        """
        self.vm.add_args('-machine', 'virt', '-device',
                         'vm-launch-update,id=fwupd1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                    r'This machine does not support vm-launch-update device')

    def test_vm_launch_update(self):
        self.aarch64_fail_test()

if __name__ == '__main__':
    QemuSystemTest.main()
