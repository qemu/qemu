#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an Orange Pi machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil

from qemu_test import LinuxKernelTest, exec_command_and_wait_for_pattern
from qemu_test import Asset, interrupt_interactive_console_until_pattern
from qemu_test import wait_for_console_pattern, skipBigDataTest
from qemu_test.utils import image_pow2ceil_expand


class OrangePiMachine(LinuxKernelTest):

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

    ASSET_ARMBIAN = Asset(
        ('https://k-space.ee.armbian.com/archive/orangepipc/archive/'
         'Armbian_23.8.1_Orangepipc_jammy_current_6.1.47.img.xz'),
        'b386dff6552513b5f164ea00f94814a6b0f1da9fb90b83725e949cf797e11afb')

    ASSET_UBOOT = Asset(
        ('http://snapshot.debian.org/archive/debian/20200108T145233Z/pool/'
         'main/u/u-boot/u-boot-sunxi_2020.01%2Bdfsg-1_armhf.deb'),
        '9223d94dc283ab54df41ce9d6f69025a5b47fece29fb67a714e23aa0cdf3bdfa')

    ASSET_NETBSD = Asset(
        ('https://archive.netbsd.org/pub/NetBSD-archive/NetBSD-9.0/'
         'evbarm-earmv7hf/binary/gzimg/armv7.img.gz'),
        '20d3e07dc057e15c12452620e90ecab2047f0f7940d9cba8182ebc795927177f')

    def test_arm_orangepi(self):
        self.set_machine('orangepi-pc')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/' +
                    'sun8i-h3-orangepi-pc.dtb')
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

    def test_arm_orangepi_initrd(self):
        self.set_machine('orangepi-pc')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/' +
                    'sun8i-h3-orangepi-pc.dtb')
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

    def test_arm_orangepi_sd(self):
        self.set_machine('orangepi-pc')
        self.require_netdev('user')
        kernel_path = self.archive_extract(
            self.ASSET_DEB, member='boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = ('usr/lib/linux-image-6.6.16-current-sunxi/' +
                    'sun8i-h3-orangepi-pc.dtb')
        dtb_path = self.archive_extract(self.ASSET_DEB, member=dtb_path)
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)
        image_pow2ceil_expand(rootfs_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'root=/dev/mmcblk0 rootwait rw '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', 'file=' + rootfs_path + ',if=sd,format=raw',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        shell_ready = "/bin/sh: can't access tty; job control turned off"
        self.wait_for_console_pattern(shell_ready)

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions',
                                                'mmcblk0')
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
    def test_arm_orangepi_armbian(self):
        self.set_machine('orangepi-pc')
        self.require_netdev('user')

        # This test download a 275 MiB compressed image and expand it
        # to 1036 MiB, but the underlying filesystem is 1552 MiB...
        # As we expand it to 2 GiB we are safe.
        image_path = self.uncompress(self.ASSET_ARMBIAN)
        image_pow2ceil_expand(image_path)

        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image_path + ',if=sd,format=raw',
                         '-nic', 'user',
                         '-no-reboot')
        self.vm.launch()

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'loglevel=7 '
                               'nosmp '
                               'systemd.default_timeout_start_sec=9000 '
                               'systemd.mask=armbian-zram-config.service '
                               'systemd.mask=armbian-ramlog.service')

        self.wait_for_console_pattern('U-Boot SPL')
        self.wait_for_console_pattern('Autoboot in ')
        exec_command_and_wait_for_pattern(self, ' ', '=>')
        exec_command_and_wait_for_pattern(self, "setenv extraargs '" +
                                                kernel_command_line + "'", '=>')
        exec_command_and_wait_for_pattern(self, 'boot', 'Starting kernel ...')

        self.wait_for_console_pattern('systemd[1]: Hostname set ' +
                                      'to <orangepipc>')
        self.wait_for_console_pattern('Starting Load Kernel Modules...')

    @skipBigDataTest()
    def test_arm_orangepi_uboot_netbsd9(self):
        self.set_machine('orangepi-pc')
        self.require_netdev('user')

        # This test download a 304MB compressed image and expand it to 2GB
        # We use the common OrangePi PC 'plus' build of U-Boot for our secondary
        # program loader (SPL). We will then set the path to the more specific
        # OrangePi "PC" device tree blob with 'setenv fdtfile' in U-Boot prompt,
        # before to boot NetBSD.
        uboot_path = 'usr/lib/u-boot/orangepi_plus/u-boot-sunxi-with-spl.bin'
        uboot_path = self.archive_extract(self.ASSET_UBOOT, member=uboot_path)
        image_path = self.uncompress(self.ASSET_NETBSD)
        image_pow2ceil_expand(image_path)
        image_drive_args = 'if=sd,format=raw,snapshot=on,file=' + image_path

        # dd if=u-boot-sunxi-with-spl.bin of=armv7.img bs=1K seek=8 conv=notrunc
        with open(uboot_path, 'rb') as f_in:
            with open(image_path, 'r+b') as f_out:
                f_out.seek(8 * 1024)
                shutil.copyfileobj(f_in, f_out)

        self.vm.set_console()
        self.vm.add_args('-nic', 'user',
                         '-drive', image_drive_args,
                         '-global', 'allwinner-rtc.base-year=2000',
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'U-Boot 2020.01+dfsg-1')
        interrupt_interactive_console_until_pattern(self,
                                       'Hit any key to stop autoboot:',
                                       'switch to partitions #0, OK')

        exec_command_and_wait_for_pattern(self, '', '=>')
        cmd = 'setenv bootargs root=ld0a'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = 'setenv kernel netbsd-GENERIC.ub'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = 'setenv fdtfile dtb/sun8i-h3-orangepi-pc.dtb'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = ("setenv bootcmd 'fatload mmc 0:1 ${kernel_addr_r} ${kernel}; "
               "fatload mmc 0:1 ${fdt_addr_r} ${fdtfile}; "
               "fdt addr ${fdt_addr_r}; "
               "bootm ${kernel_addr_r} - ${fdt_addr_r}'")
        exec_command_and_wait_for_pattern(self, cmd, '=>')

        exec_command_and_wait_for_pattern(self, 'boot',
                                          'Booting kernel from Legacy Image')
        wait_for_console_pattern(self, 'Starting kernel ...')
        wait_for_console_pattern(self, 'NetBSD 9.0 (GENERIC)')
        # Wait for user-space
        wait_for_console_pattern(self, 'Starting root file system check')

if __name__ == '__main__':
    LinuxKernelTest.main()
