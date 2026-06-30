#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Tenstorrent Atlantis machine
# and checks the console
#
# Copyright (c) Linaro Ltd.
# Copyright 2026 Tenstorrent
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest


class TTAtlantis(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.11.9/riscv64/Image',
        '174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0')
    ASSET_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '9819da19e6eef291686fdd7b029ea00e764dc62f/rootfs/riscv64/'
         'rootfs.ext2.gz'),
        'b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b')

    def do_test_riscv64_tt_atlantis(self, connect_disk):
        self.set_machine('tt-atlantis')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'earlycon=sbi '

        if connect_disk:
            kernel_command_line += 'root=/dev/vda panic=-1 noreboot rootwait '
            self.vm.add_args('-device',
                             'virtio-blk,drive=drive0,serial=0x1234,bus=pcie.0')
            self.vm.add_args('-drive',
                             f'file={rootfs_path},if=none,id=drive0,format=raw')
            pattern = 'Boot successful.'
        else:
            kernel_command_line += 'panic=0 noreboot '
            pattern = 'Cannot open root device'

        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line,
                         '-no-reboot')

        self.vm.launch()
        self.wait_for_console_pattern(pattern)

    def test_riscv64_tt_atlantis(self):
        # tt-atlantis machine has no PCI host yet, so no disk
        self.do_test_riscv64_tt_atlantis(False)


if __name__ == '__main__':
    LinuxKernelTest.main()
