# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class N8x0Machine(QemuSystemTest):
    """Boots the Linux kernel and checks that the console is operational"""

    timeout = 90

    def __do_test_n8x0(self):
        kernel_url = ('http://stskeeps.subnetmask.net/meego-n8x0/'
                      'meego-arm-n8x0-1.0.80.20100712.1431-'
                      'vmlinuz-2.6.35~rc4-129.1-n8x0')
        kernel_hash = 'e9d5ab8d7548923a0061b6fbf601465e479ed269'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console(console_index=1)
        self.vm.add_args('-kernel', kernel_path,
                         '-append', 'printk.time=0 console=ttyS1')
        self.vm.launch()
        wait_for_console_pattern(self, 'TSC2005 driver initializing')

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_n800(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:n800
        """
        self.__do_test_n8x0()

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_n810(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:n810
        """
        self.__do_test_n8x0()
