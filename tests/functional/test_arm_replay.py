#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on arm machines and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class ArmReplay(ReplayKernelBase):

    ASSET_VIRT = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/29/Everything/armhfp/os/images/pxeboot/vmlinuz'),
        '18dd5f1a9a28bd539f9d047f7c0677211bae528e8712b40ca5a229a4ad8e2591')

    def test_virt(self):
        self.set_machine('virt')
        kernel_path = self.ASSET_VIRT.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=1)

    ASSET_CUBIE_KERNEL = Asset(
        ('https://apt.armbian.com/pool/main/l/linux-6.6.16/'
         'linux-image-current-sunxi_24.2.1_armhf_'
         '_6.6.16-Seb3e-D6b4a-P2359-Ce96bHfe66-HK01ba-V014b-B067e-R448a.deb'),
        '3d968c15b121ede871dce49d13ee7644d6f74b6b121b84c9a40f51b0c80d6d22')

    ASSET_CUBIE_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/arm/rootfs-armv5.cpio.gz'),
        '334b8d256db67a3f2b3ad070aa08b5ade39624e0e7e35b02f4359a577bc8f39b')

    def test_cubieboard(self):
        self.set_machine('cubieboard')
        kernel_path = self.archive_extract(self.ASSET_CUBIE_KERNEL,
            member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = self.archive_extract(self.ASSET_CUBIE_KERNEL,
            member='usr/lib/linux-image-6.6.16-current-sunxi/sun4i-a10-cubieboard.dtb')
        initrd_path = self.uncompress(self.ASSET_CUBIE_INITRD)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'panic=-1 noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=1,
                    args=('-dtb', dtb_path,
                          '-initrd', initrd_path,
                          '-no-reboot'))

    ASSET_DAY16 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day16.tar.xz',
        '63311adb2d4c4e7a73214a86d29988add87266a909719c56acfadd026b4110a7')

    def test_vexpressa9(self):
        self.set_machine('vexpress-a9')
        self.archive_extract(self.ASSET_DAY16)
        kernel_path = self.scratch_file('day16', 'winter.zImage')
        dtb_path = self.scratch_file('day16', 'vexpress-v2p-ca9.dtb')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar', args=('-dtb', dtb_path))


if __name__ == '__main__':
    ReplayKernelBase.main()
