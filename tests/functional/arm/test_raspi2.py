#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Raspberry Pi machine
# and checks the console
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class ArmRaspi2Machine(LinuxKernelTest):

    ASSET_KERNEL_20190215 = Asset(
        ('http://archive.raspberrypi.org/debian/'
         'pool/main/r/raspberrypi-firmware/'
         'raspberrypi-kernel_1.20190215-1_armhf.deb'),
        '9f1759f7228113da24f5ee2aa6312946ec09a83e076aba9406c46ff776dfb291')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
         'arm/rootfs-armv7a.cpio.gz'),
        '2c8dbdb16ea7af2dfbcbea96044dde639fb07d09fd3c4fb31f2027ef71e55ddd')

    def do_test_arm_raspi2(self, uart_id):
        """
        The kernel can be rebuilt using the kernel source referenced
        and following the instructions on the on:
        https://www.raspberrypi.org/documentation/linux/kernel/building.md
        """
        serial_kernel_cmdline = {
            0: 'earlycon=pl011,0x3f201000 console=ttyAMA0',
        }
        kernel_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                           member='boot/kernel7.img')
        dtb_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                        member='boot/bcm2709-rpi-2-b.dtb')

        self.set_machine('raspi2b')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               serial_kernel_cmdline[uart_id] +
                               ' root=/dev/mmcblk0p2 rootwait ' +
                               'dwc_otg.fiq_fsm_enable=0')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line,
                         '-device', 'usb-kbd')
        self.vm.launch()

        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        self.wait_for_console_pattern('Product: QEMU USB Keyboard')

    def test_arm_raspi2_uart0(self):
        self.do_test_arm_raspi2(0)

    def test_arm_raspi2_initrd(self):
        kernel_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                           member='boot/kernel7.img')
        dtb_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                        member='boot/bcm2709-rpi-2-b.dtb')
        initrd_path = self.uncompress(self.ASSET_INITRD)

        self.set_machine('raspi2b')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'earlycon=pl011,0x3f201000 console=ttyAMA0 '
                               'panic=-1 noreboot ' +
                               'dwc_otg.fiq_fsm_enable=0')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'BCM2835')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                '/soc/cprman@7e101000')
        exec_command_and_wait_for_pattern(self, 'halt', 'reboot: System halted')
        # Wait for VM to shut down gracefully
        self.vm.wait()


if __name__ == '__main__':
    LinuxKernelTest.main()
