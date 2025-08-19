#!/usr/bin/env python3
#
# Copyright (c) 2024 Linaro Ltd.
#
# Functional test that boots a Linux kernel on an sx1 machine
# and checks the console. We have three variants:
#  * just boot initrd
#  * boot with filesystem on SD card
#  * boot from flash
# In all cases these images have a userspace that is configured
# to immediately reboot the system on successful boot, so we
# only need to wait for QEMU to exit (via -no-reboot).
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class SX1Test(LinuxKernelTest):

    ASSET_ZIMAGE = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/sx1/zImage',
        'a0271899a8dc2165f9e0adb2d0a57fc839ae3a469722ffc56c77e108a8887615')

    ASSET_INITRD = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/sx1/rootfs-armv4.cpio',
        '35b0721249821aa544cd85b85d3cb8901db4c6d128eed86ab261e5d9e37d58f8')

    ASSET_SD_FS = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/sx1/rootfs-armv4.ext2',
        'c1db7f43ef92469ebc8605013728c8950e7608439f01d13678994f0ce101c3a8')

    ASSET_FLASH = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/sx1/flash',
        '17e6a2758fa38efd2666be0879d4751fd37d194f25168a8deede420df519b676')

    CONSOLE_ARGS = 'console=ttyS0,115200 earlycon=uart8250,mmio32,0xfffb0000,115200n8'

    def test_arm_sx1_initrd(self):
        self.set_machine('sx1')
        zimage_path = self.ASSET_ZIMAGE.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        self.vm.add_args('-append', f'kunit.enable=0 rdinit=/sbin/init {self.CONSOLE_ARGS}')
        self.vm.add_args('-no-reboot')
        self.launch_kernel(zimage_path,
                           initrd=initrd_path,
                           wait_for='Boot successful')
        self.vm.wait(timeout=120)

    def test_arm_sx1_sd(self):
        self.set_machine('sx1')
        zimage_path = self.ASSET_ZIMAGE.fetch()
        sd_fs_path = self.ASSET_SD_FS.fetch()
        self.vm.add_args('-append', f'kunit.enable=0 root=/dev/mmcblk0 rootwait {self.CONSOLE_ARGS}')
        self.vm.add_args('-no-reboot')
        self.vm.add_args('-snapshot')
        self.vm.add_args('-drive', f'format=raw,if=sd,file={sd_fs_path}')
        self.launch_kernel(zimage_path, wait_for='Boot successful')
        self.vm.wait(timeout=120)

    def test_arm_sx1_flash(self):
        self.set_machine('sx1')
        zimage_path = self.ASSET_ZIMAGE.fetch()
        flash_path = self.ASSET_FLASH.fetch()
        self.vm.add_args('-append', f'kunit.enable=0 root=/dev/mtdblock3 rootwait {self.CONSOLE_ARGS}')
        self.vm.add_args('-no-reboot')
        self.vm.add_args('-snapshot')
        self.vm.add_args('-drive', f'format=raw,if=pflash,file={flash_path}')
        self.launch_kernel(zimage_path, wait_for='Boot successful')
        self.vm.wait(timeout=120)

if __name__ == '__main__':
    LinuxKernelTest.main()
