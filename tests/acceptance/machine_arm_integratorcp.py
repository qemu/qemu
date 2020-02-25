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
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern

class IntegratorMachine(Test):

    timeout = 90

    def boot_integratorcp(self):
        kernel_url = ('https://github.com/zayac/qemu-arm/raw/master/'
                      'arm-test/kernel/zImage.integrator')
        kernel_hash = '0d7adba893c503267c946a3cbdc63b4b54f25468'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('https://github.com/zayac/qemu-arm/raw/master/'
                      'arm-test/kernel/arm_root.img')
        initrd_hash = 'b51e4154285bf784e017a37586428332d8c7bd8b'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', 'printk.time=0 console=ttyAMA0')
        self.vm.launch()

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_integratorcp_console(self):
        """
        Boots the Linux kernel and checks that the console is operational
        :avocado: tags=arch:arm
        :avocado: tags=machine:integratorcp
        :avocado: tags=device:pl011
        """
        self.boot_integratorcp()
        wait_for_console_pattern(self, 'Log in as root')
