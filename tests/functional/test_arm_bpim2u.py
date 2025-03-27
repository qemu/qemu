#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Banana Pi machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import LinuxKernelTest, exec_command_and_wait_for_pattern
from qemu_test import Asset, interrupt_interactive_console_until_pattern
from qemu_test import skipBigDataTest
from qemu_test.utils import image_pow2ceil_expand


class BananaPiMachine(LinuxKernelTest):

    ASSET_DEB = Asset(
        ('https://apt.armbian.com/pool/main/l/linux-6.6.16/'
         'linux-image-current-sunxi_24.2.1_armhf__6.6.16-Seb3e-D6b4a-P2359-Ce96bHfe66-HK01ba-V014b-B067e-R448a.deb'),
        '3d968c15b121ede871dce49d13ee7644d6f74b6b121b84c9a40f51b0c80d6d22')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
         'arm/rootfs-armv7a.cpio.gz'),
        '2c8dbdb16ea7af2dfbcbea96044dde639fb07d09fd3c4fb31f2027ef71e55ddd')

    ASSET_ROOTFS = Asset(
        ('http://storage.kernelci.org/images/rootfs/buildroot/'
         'buildroot-baseline/20230703.0/armel/rootfs.ext2.xz'),
        '42b44a12965ac0afe9a88378527fb698a7dc76af50495efc2361ee1595b4e5c6')

    ASSET_SD_IMAGE = Asset(
        ('https://downloads.openwrt.org/releases/22.03.3/targets/sunxi/cortexa7/'
         'openwrt-22.03.3-sunxi-cortexa7-sinovoip_bananapi-m2-ultra-ext4-sdcard.img.gz'),
        '5b41b4e11423e562c6011640f9a7cd3bdd0a3d42b83430f7caa70a432e6cd82c')

    def test_arm_bpim2u(self):
        self.set_machine('bpim2u')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/'
                    'sun8i-r40-bananapi-m2-ultra.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200n8 '
                               'earlycon=uart,mmio32,0x1c28000')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        os.remove(kernel_path)
        os.remove(dtb_path)

    def test_arm_bpim2u_initrd(self):
        self.set_machine('bpim2u')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/'
                    'sun8i-r40-bananapi-m2-ultra.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)
        initrd_path = self.uncompress(self.ASSET_INITRD)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'system-control@1c00000')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()
        os.remove(kernel_path)
        os.remove(dtb_path)
        os.remove(initrd_path)

    def test_arm_bpim2u_gmac(self):
        self.set_machine('bpim2u')
        self.require_netdev('user')

        deb_path = self.ASSET_DEB.fetch()
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/'
                    'sun8i-r40-bananapi-m2-ultra.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)
        image_pow2ceil_expand(rootfs_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'root=b300 rootwait rw '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', 'file=' + rootfs_path + ',if=sd,format=raw',
                         '-net', 'nic,model=gmac,netdev=host_gmac',
                         '-netdev', 'user,id=host_gmac',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        shell_ready = "/bin/sh: can't access tty; job control turned off"
        self.wait_for_console_pattern(shell_ready)

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions',
                                                'mmcblk')
        exec_command_and_wait_for_pattern(self, 'ifconfig eth0 up',
                                                 'eth0: Link is Up')
        exec_command_and_wait_for_pattern(self, 'udhcpc eth0',
            'udhcpc: lease of 10.0.2.15 obtained')
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
            '3 packets transmitted, 3 packets received, 0% packet loss')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()
        os.remove(kernel_path)
        os.remove(dtb_path)
        os.remove(rootfs_path)

    @skipBigDataTest()
    def test_arm_bpim2u_openwrt_22_03_3(self):
        self.set_machine('bpim2u')
        self.require_netdev('user')

        # This test download a 8.9 MiB compressed image and expand it
        # to 127 MiB.
        image_path = self.uncompress(self.ASSET_SD_IMAGE)
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
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'system-control@1c00000')
        os.remove(image_path)

if __name__ == '__main__':
    LinuxKernelTest.main()
