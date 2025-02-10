#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Raspberry Pi machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class Aarch64Raspi4Machine(LinuxKernelTest):

    """
    The kernel can be rebuilt using the kernel source referenced
    and following the instructions on the on:
    https://www.raspberrypi.org/documentation/linux/kernel/building.md
    """
    ASSET_KERNEL_20190215 = Asset(
        ('http://archive.raspberrypi.org/debian/'
         'pool/main/r/raspberrypi-firmware/'
         'raspberrypi-kernel_1.20230106-1_arm64.deb'),
        '56d5713c8f6eee8a0d3f0e73600ec11391144fef318b08943e9abd94c0a9baf7')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '86b2be1384d41c8c388e63078a847f1e1c4cb1de/rootfs/'
         'arm64/rootfs.cpio.gz'),
        '7c0b16d1853772f6f4c3ca63e789b3b9ff4936efac9c8a01fb0c98c05c7a7648')

    def test_arm_raspi4(self):
        kernel_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                           member='boot/kernel8.img')
        dtb_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                        member='boot/bcm2711-rpi-4-b.dtb')

        self.set_machine('raspi4b')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'earlycon=pl011,mmio32,0xfe201000 ' +
                               'console=ttyAMA0,115200 ' +
                               'root=/dev/mmcblk1p2 rootwait ' +
                               'dwc_otg.fiq_fsm_enable=0')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        # When PCI is supported we can add a USB controller:
        #                '-device', 'qemu-xhci,bus=pcie.1,id=xhci',
        #                '-device', 'usb-kbd,bus=xhci.0',
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        # When USB is enabled we can look for this
        # console_pattern = 'Product: QEMU USB Keyboard'
        # self.wait_for_console_pattern(console_pattern)
        console_pattern = 'Waiting for root device'
        self.wait_for_console_pattern(console_pattern)


    def test_arm_raspi4_initrd(self):
        kernel_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                           member='boot/kernel8.img')
        dtb_path = self.archive_extract(self.ASSET_KERNEL_20190215,
                                        member='boot/bcm2711-rpi-4-b.dtb')
        initrd_path = self.uncompress(self.ASSET_INITRD)

        self.set_machine('raspi4b')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'earlycon=pl011,mmio32,0xfe201000 ' +
                               'console=ttyAMA0,115200 ' +
                               'panic=-1 noreboot ' +
                               'dwc_otg.fiq_fsm_enable=0')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        # When PCI is supported we can add a USB controller:
        #                '-device', 'qemu-xhci,bus=pcie.1,id=xhci',
        #                '-device', 'usb-kbd,bus=xhci.0',
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'BCM2835')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'cprman@7e101000')
        exec_command_and_wait_for_pattern(self, 'halt', 'reboot: System halted')
        # TODO: Raspberry Pi4 doesn't shut down properly with recent kernels
        # Wait for VM to shut down gracefully
        #self.vm.wait()


if __name__ == '__main__':
    LinuxKernelTest.main()
