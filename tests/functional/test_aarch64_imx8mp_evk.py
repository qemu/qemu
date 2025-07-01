#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class Imx8mpEvkMachine(LinuxKernelTest):

    ASSET_IMAGE = Asset(
        ('https://cloud.debian.org/images/cloud/bookworm/20231210-1590/'
         'debian-12-generic-arm64-20231210-1590.tar.xz'),
        '7ebf1577b32d5af6204df74b54ca2e4675de9b5a9fa14f3ff70b88eeb7b3b359')

    KERNEL_OFFSET = 0x51000000
    KERNEL_SIZE = 32622528
    INITRD_OFFSET = 0x76000000
    INITRD_SIZE = 30987766
    DTB_OFFSET = 0x64F51000
    DTB_SIZE = 45 * 1024

    def extract(self, in_path, out_path, offset, size):
        try:
            with open(in_path, "rb") as source:
                source.seek(offset)
                data = source.read(size)
            with open(out_path, "wb") as target:
                target.write(data)
        except (IOError, ValueError) as e:
            self.log.error(f"Failed to extract {out_path}: {e}")
            raise

    def setUp(self):
        super().setUp()

        self.image_path = self.scratch_file("disk.raw")
        self.kernel_path = self.scratch_file("linux")
        self.initrd_path = self.scratch_file("initrd.zstd")
        self.dtb_path = self.scratch_file("imx8mp-evk.dtb")

        self.archive_extract(self.ASSET_IMAGE)
        self.extract(self.image_path, self.kernel_path,
                     self.KERNEL_OFFSET, self.KERNEL_SIZE)
        self.extract(self.image_path, self.initrd_path,
                     self.INITRD_OFFSET, self.INITRD_SIZE)
        self.extract(self.image_path, self.dtb_path,
                     self.DTB_OFFSET, self.DTB_SIZE)

    def test_aarch64_imx8mp_evk_usdhc(self):
        self.require_accelerator("tcg")
        self.set_machine('imx8mp-evk')
        self.vm.set_console(console_index=1)
        self.vm.add_args('-m', '2G',
                         '-smp', '4',
                         '-kernel', self.kernel_path,
                         '-initrd', self.initrd_path,
                         '-dtb', self.dtb_path,
                         '-append', 'root=/dev/mmcblk2p1',
                         '-drive', f'file={self.image_path},if=sd,bus=2,'
                                    'format=raw,id=mmcblk2,snapshot=on')

        self.vm.launch()
        self.wait_for_console_pattern('Welcome to ')

if __name__ == '__main__':
    LinuxKernelTest.main()
