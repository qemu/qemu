#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import interrupt_interactive_console_until_pattern, skipSlowTest


class EmcraftSf2Machine(LinuxKernelTest):

    ASSET_IMAGE = Asset(
        ('https://github.com/hskinnemoen/openbmc/releases/download/'
         '20200711-gsj-qemu-0/obmc-phosphor-image-gsj.static.mtd.gz'),
        'eccd4e375cde53034c84aece5c511932cacf838d9fd3f63da368a511757da72b')

    ASSET_INITRD = Asset(
        ('https://github.com/hskinnemoen/openbmc/releases/download/'
         '20200711-gsj-qemu-0/obmc-phosphor-initramfs-gsj.cpio.xz'),
        '37b05009fc54db1434beac12bd7ff99a2e751a2f032ee18d9042f991dd0cdeaa')

    ASSET_KERNEL = Asset(
        ('https://github.com/hskinnemoen/openbmc/releases/download/'
         '20200711-gsj-qemu-0/uImage-gsj.bin'),
        'ce6d6b37bff46c74fc7b1e90da10a431cc37a62cdb35ec199fa73473d0790110')

    ASSET_DTB = Asset(
        ('https://github.com/hskinnemoen/openbmc/releases/download/'
         '20200711-gsj-qemu-0/nuvoton-npcm730-gsj.dtb'),
        '3249b2da787d4b9ad4e61f315b160abfceb87b5e1895a7ce898ce7f40c8d4045')

    @skipSlowTest()
    def test_arm_quanta_gsj(self):
        self.set_machine('quanta-gsj')
        image_path = self.uncompress(self.ASSET_IMAGE, format='gz')

        self.vm.set_console()
        drive_args = 'file=' + image_path + ',if=mtd,bus=0,unit=0'
        self.vm.add_args('-drive', drive_args)
        self.vm.launch()

        # Disable drivers and services that stall for a long time during boot,
        # to avoid running past the 90-second timeout. These may be removed
        # as the corresponding device support is added.
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + (
                'console=${console} '
                'mem=${mem} '
                'initcall_blacklist=npcm_i2c_bus_driver_init '
                'systemd.mask=systemd-random-seed.service '
                'systemd.mask=dropbearkey.service '
        )

        self.wait_for_console_pattern('> BootBlock by Nuvoton')
        self.wait_for_console_pattern('>Device: Poleg BMC NPCM730')
        self.wait_for_console_pattern('>Skip DDR init.')
        self.wait_for_console_pattern('U-Boot ')
        interrupt_interactive_console_until_pattern(
                self, 'Hit any key to stop autoboot:', 'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, "setenv bootargs ${bootargs} " + kernel_command_line,
                'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, 'run romboot', 'Booting Kernel from flash')
        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern('OpenBMC Project Reference Distro')
        self.wait_for_console_pattern('gsj login:')

    def test_arm_quanta_gsj_initrd(self):
        self.set_machine('quanta-gsj')
        initrd_path = self.ASSET_INITRD.fetch()
        kernel_path = self.ASSET_KERNEL.fetch()
        dtb_path = self.ASSET_DTB.fetch()

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200n8 '
                               'earlycon=uart8250,mmio32,0xf0001000')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        self.vm.launch()

        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern(
                'Give root password for system maintenance')

if __name__ == '__main__':
    LinuxKernelTest.main()
