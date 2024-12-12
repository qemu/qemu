#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test.utils import gzip_uncompress

class Smdkc210Machine(LinuxKernelTest):

    ASSET_DEB = Asset(
        ('https://snapshot.debian.org/archive/debian/20190928T224601Z/pool/'
         'main/l/linux/linux-image-4.19.0-6-armmp_4.19.67-2+deb10u1_armhf.deb'),
        '421804e7579ef40d554c962850dbdf1bfc79f7fa7faec9d391397170dc806c3e')

    ASSET_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/arm/'
         'rootfs-armv5.cpio.gz'),
        '334b8d256db67a3f2b3ad070aa08b5ade39624e0e7e35b02f4359a577bc8f39b')

    def test_arm_exynos4210_initrd(self):
        self.set_machine('smdkc210')

        deb_path = self.ASSET_DEB.fetch()
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.19.0-6-armmp')
        dtb_path = '/usr/lib/linux-image-4.19.0-6-armmp/exynos4210-smdkv310.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)

        initrd_path_gz = self.ASSET_ROOTFS.fetch()
        initrd_path = os.path.join(self.workdir, 'rootfs.cpio')
        gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'earlycon=exynos4210,0x13800000 earlyprintk ' +
                               'console=ttySAC0,115200n8 ' +
                               'random.trust_cpu=off cryptomgr.notests ' +
                               'cpuidle.off=1 panic=-1 noreboot')

        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()

        self.wait_for_console_pattern('Boot successful.')
        # TODO user command, for now the uart is stuck

if __name__ == '__main__':
    LinuxKernelTest.main()
