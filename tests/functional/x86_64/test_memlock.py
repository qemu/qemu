#!/usr/bin/env python3
#
# Functional test that check overcommit memlock options
#
# Copyright (c) Yandex Technologies LLC, 2025
#
# Author:
#  Alexandr Moshkov <dtalexundeer@yandex-team.ru>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import re

from typing import Dict

from qemu_test import QemuSystemTest
from qemu_test import skipLockedMemoryTest


STATUS_VALUE_PATTERN = re.compile(r'^(\w+):\s+(\d+) kB', re.MULTILINE)


@skipLockedMemoryTest(2_097_152)  # 2GB
class MemlockTest(QemuSystemTest):
    """
    Runs a guest with memlock options.
    Then verify, that this options is working correctly
    by checking the status file of the QEMU process.
    """

    def common_vm_setup_with_memlock(self, memlock):
        self.vm.add_args('-overcommit', f'mem-lock={memlock}')
        self.vm.launch()

    def test_memlock_off(self):
        self.common_vm_setup_with_memlock('off')

        status = self.get_process_status_values(self.vm.get_pid())

        # libgcrypt may mlock a few pages
        self.assertTrue(status['VmLck'] < 32)

    def test_memlock_on(self):
        self.common_vm_setup_with_memlock('on')

        status = self.get_process_status_values(self.vm.get_pid())

        # VmLck > 0 kB and almost all memory is resident
        self.assertTrue(status['VmLck'] > 0)
        self.assertTrue(status['VmRSS'] >= status['VmSize'] * 0.70)

    def test_memlock_onfault(self):
        self.common_vm_setup_with_memlock('on-fault')

        status = self.get_process_status_values(self.vm.get_pid())

        # VmLck > 0 kB and only few memory is resident
        self.assertTrue(status['VmLck'] > 0)
        self.assertTrue(status['VmRSS'] <= status['VmSize'] * 0.30)

    def get_process_status_values(self, pid: int) -> Dict[str, int]:
        result = {}
        raw_status = self._get_raw_process_status(pid)

        for line in raw_status.split('\n'):
            if m := STATUS_VALUE_PATTERN.match(line):
                result[m.group(1)] = int(m.group(2))

        return result

    def _get_raw_process_status(self, pid: int) -> str:
        status = None
        try:
            with open(f'/proc/{pid}/status', 'r', encoding="ascii") as f:
                status = f.read()
        except FileNotFoundError:
            self.skipTest("Can't open status file of the process")
        return status


if __name__ == '__main__':
    MemlockTest.main()
