#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test import skipBigDataTest
from qemu_test.utils import image_pow2ceil_expand


class CubieboardMachine(LinuxKernelTest):

    ASSET_DEB = Asset(
        ('https://apt.armbian.com/pool/main/l/linux-6.6.16/'
         'linux-image-current-sunxi_24.2.1_armhf__6.6.16-Seb3e-D6b4a-P2359-Ce96bHfe66-HK01ba-V014b-B067e-R448a.deb'),
        '3d968c15b121ede871dce49d13ee7644d6f74b6b121b84c9a40f51b0c80d6d22')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
         'arm/rootfs-armv5.cpio.gz'),
        '334b8d256db67a3f2b3ad070aa08b5ade39624e0e7e35b02f4359a577bc8f39b')

    ASSET_SATA_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
         'arm/rootfs-armv5.ext2.gz'),
        '17fc750da568580b39372133051ef2f0a963c0c0b369b845614442d025701745')

    ASSET_OPENWRT = Asset(
        ('https://downloads.openwrt.org/releases/22.03.2/targets/sunxi/cortexa8/'
         'openwrt-22.03.2-sunxi-cortexa8-cubietech_a10-cubieboard-ext4-sdcard.img.gz'),
        '94b5ecbfbc0b3b56276e5146b899eafa2ac5dc2d08733d6705af9f144f39f554')

    def test_arm_cubieboard_initrd(self):
        self.set_machine('cubieboard')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/' +
                    'sun4i-a10-cubieboard.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)
        initrd_path = self.uncompress(self.ASSET_INITRD)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'system-control@1c00000')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    def test_arm_cubieboard_sata(self):
        self.set_machine('cubieboard')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/' +
                    'sun4i-a10-cubieboard.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)

        rootfs_path = self.uncompress(self.ASSET_SATA_ROOTFS)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'root=/dev/sda ro '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', 'if=none,format=raw,id=disk0,file='
                                   + rootfs_path,
                         '-device', 'ide-hd,bus=ide.0,drive=disk0',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions',
                                                'sda')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    @skipBigDataTest()
    def test_arm_cubieboard_openwrt_22_03_2(self):
        # This test download a 7.5 MiB compressed image and expand it
        # to 126 MiB.
        self.set_machine('cubieboard')
        self.require_netdev('user')

        image_path = self.uncompress(self.ASSET_OPENWRT)
        image_pow2ceil_expand(image_path)

        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image_path + ',if=sd,format=raw',
                         '-nic', 'user',
                         '-no-reboot')
        self.vm.launch()

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'usbcore.nousb '
                               'noreboot')

        self.wait_for_console_pattern('U-Boot SPL')

        interrupt_interactive_console_until_pattern(
                self, 'Hit any key to stop autoboot:', '=>')
        exec_command_and_wait_for_pattern(self, "setenv extraargs '" +
                                                kernel_command_line + "'", '=>')
        exec_command_and_wait_for_pattern(self, 'boot', 'Starting kernel ...')

        self.wait_for_console_pattern(
            'Please press Enter to activate this console.')

        exec_command_and_wait_for_pattern(self, ' ', 'root@')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

if __name__ == '__main__':
    LinuxKernelTest.main()
