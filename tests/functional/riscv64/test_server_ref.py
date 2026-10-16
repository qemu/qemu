#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
"""
riscv-server-ref board test
"""

import os
import tempfile
import subprocess

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import skipIfMissingCommands

class RiscvServerRefTest(QemuSystemTest):
    """
    Test the riscv-server-ref board
    """

    ASSET_KERNEL = Asset(
        ('https://github.com/danielhb/qemu-machine-boot/raw/refs/heads/'
         'master/riscv/images/virt64/buildroot/Image'),
        '6bacc876c769c1bb6057d2bf549eba67fbe83916e8223f9fe21c8e8fff665a36')

    ASSET_ROOTFS = Asset(
        ('https://github.com/danielhb/qemu-machine-boot/raw/refs/heads/'
         'master/riscv/images/virt64/buildroot/rootfs.ext2'),
        'f00bb88749f945d80675540a1338bd1ccb226574685a5b6c65ab44027d0411a8')

    def do_test_boot_linux_test(self, tpmstate_dir=None):
        self.set_machine('riscv-server-ref')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.vm.add_args('-kernel', kernel_path)
        self.vm.add_args('-append', 'rw rootwait root=/dev/sda')
        self.vm.add_args('-drive',
            f'file={rootfs_path},format=raw,id=hd0,snapshot=on,if=none')
        self.vm.add_args('-device', 'ich9-ahci,id=ahci0')
        self.vm.add_args('-device', 'ide-hd,drive=hd0,bus=ahci0.0')
        self.vm.add_args('-netdev', 'user,id=net0')
        self.vm.add_args('-device', 'e1000e,netdev=net0')

        if tpmstate_dir is not None:
            # Note: code taken verbatim from
            # tests/functional/arm/test_aspeed_ast2600_buildroot_tpm.py

            # We must put the TPM state dir in /tmp/, not the build dir,
            # because some distros use AppArmor to lock down swtpm and
            # restrict the set of locations it can access files in.
            socket = os.path.join(tpmstate_dir, 'swtpm-socket')
            subprocess.run(['swtpm', 'socket', '-d', '--tpm2',
                            '--tpmstate', f'dir={tpmstate_dir}',
                            '--ctrl', f'type=unixio,path={socket}'],
                           check=True)
            self.vm.add_args('-chardev', f'socket,id=chrtpm,path={socket}')
            self.vm.add_args('-tpmdev', 'emulator,id=tpm0,chardev=chrtpm')
            self.vm.add_args('-device', 'tpm-tis-device,tpmdev=tpm0')

        self.vm.set_console()
        self.vm.launch()

        # Wait for OpenSBI
        wait_for_console_pattern(self, 'OpenSBI')

        # Wait for Linux kernel boot
        wait_for_console_pattern(self, 'Linux version')
        wait_for_console_pattern(self, 'Machine model: RISC-V Server Platform')

        # Test e1000e network card functionality
        wait_for_console_pattern(self, 'e1000e')
        wait_for_console_pattern(self, 'Network Connection')

        # Wait for boot to complete - system reaches login prompt
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

    def test_boot_linux_test(self):
        self.do_test_boot_linux_test()

    @skipIfMissingCommands('swtpm')
    def test_boot_linux_test_tpm(self):
        with tempfile.TemporaryDirectory(prefix="qemu_") as tpmstate_dir:
            self.do_test_boot_linux_test(tpmstate_dir)

if __name__ == '__main__':
    QemuSystemTest.main()
