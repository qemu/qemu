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
import time

class VmLaunchUpdateDeviceCheck(QemuSystemTest):
    DELAY_BOOT_SEQUENCE = 1

    def vm_launch_update_pass(self):
        """
        Basic test to make sure vm-launch-update device can be instantiated.
        """
        self.vm.add_args('-device', 'vm-launch-update,id=fwupd1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertEqual(self.vm.exitcode(), 0, "QEMU exit code should be 0")

    def multiple_device_fail(self):
        """
        Only one vm-launch-update device can be instantiated. Ensure failure if
        user tries to create more than one device.
        """
        self.vm.add_args('-device', 'vm-launch-update,id=fw1',
                         '-device', 'vm-launch-update,id=fw2')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                         r'at most one vm-launch-update device is permitted')

    def test_vm_launch_update(self):
        self.vm_launch_update_pass()
        self.multiple_device_fail()

if __name__ == '__main__':
    QemuSystemTest.main()
