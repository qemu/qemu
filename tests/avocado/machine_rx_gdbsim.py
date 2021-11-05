# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado import skipIf
from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive


class RxGdbSimMachine(Test):

    timeout = 30
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_uboot(self):
        """
        U-Boot and checks that the console is operational.

        :avocado: tags=arch:rx
        :avocado: tags=machine:gdbsim-r5f562n8
        :avocado: tags=endian:little
        """
        uboot_url = ('https://acc.dl.osdn.jp/users/23/23888/u-boot.bin.gz')
        uboot_hash = '9b78dbd43b40b2526848c0b1ce9de02c24f4dcdb'
        uboot_path = self.fetch_asset(uboot_url, asset_hash=uboot_hash)
        uboot_path = archive.uncompress(uboot_path, self.workdir)

        self.vm.set_console()
        self.vm.add_args('-bios', uboot_path,
                         '-no-reboot')
        self.vm.launch()
        uboot_version = 'U-Boot 2016.05-rc3-23705-ga1ef3c71cb-dirty'
        wait_for_console_pattern(self, uboot_version)
        gcc_version = 'rx-unknown-linux-gcc (GCC) 9.0.0 20181105 (experimental)'
        # FIXME limit baudrate on chardev, else we type too fast
        #exec_command_and_wait_for_pattern(self, 'version', gcc_version)

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_linux_sash(self):
        """
        Boots a Linux kernel and checks that the console is operational.

        :avocado: tags=arch:rx
        :avocado: tags=machine:gdbsim-r5f562n7
        :avocado: tags=endian:little
        """
        dtb_url = ('https://acc.dl.osdn.jp/users/23/23887/rx-virt.dtb')
        dtb_hash = '7b4e4e2c71905da44e86ce47adee2210b026ac18'
        dtb_path = self.fetch_asset(dtb_url, asset_hash=dtb_hash)
        kernel_url = ('http://acc.dl.osdn.jp/users/23/23845/zImage')
        kernel_hash = '39a81067f8d72faad90866ddfefa19165d68fc99'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'earlycon'
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'Sash command shell (version 1.1.1)',
                                 failure_message='Kernel panic - not syncing')
        exec_command_and_wait_for_pattern(self, 'printenv', 'TERM=linux')
