#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest

class RainierMachine(AspeedTest):

    ASSET_RAINIER_EMMC = Asset(
        ('https://fileserver.linaro.org/s/B6pJTwWEkzSDi36/download/'
         'mmc-p10bmc-20240617.qcow2'),
        'd523fb478d2b84d5adc5658d08502bc64b1486955683814f89c6137518acd90b')

    def test_arm_aspeed_emmc_boot(self):
        self.set_machine('rainier-bmc')
        self.require_netdev('user')

        image_path = self.ASSET_RAINIER_EMMC.fetch()

        self.vm.set_console()
        self.vm.add_args('-drive',
                         'file=' + image_path + ',if=sd,id=sd2,index=2',
                         '-net', 'nic', '-net', 'user', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot SPL 2019.04')
        self.wait_for_console_pattern('Trying to boot from MMC1')
        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('eMMC 2nd Boot')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Booting Linux on physical CPU 0xf00')
        self.wait_for_console_pattern('mmcblk0: p1 p2 p3 p4 p5 p6 p7')
        self.wait_for_console_pattern('IBM eBMC (OpenBMC for IBM Enterprise')

    ASSET_DEBIAN_LINUX_ARMHF_DEB = Asset(
            ('http://snapshot.debian.org/archive/debian/20220606T211338Z/pool/main/l/linux/linux-image-5.17.0-2-armmp_5.17.6-1%2Bb1_armhf.deb'),
        '8acb2b4439faedc2f3ed4bdb2847ad4f6e0491f73debaeb7f660c8abe4dcdc0e')

    def test_arm_debian_kernel_boot(self):
        self.set_machine('rainier-bmc')

        kernel_path = self.archive_extract(
            self.ASSET_DEBIAN_LINUX_ARMHF_DEB,
            member='boot/vmlinuz-5.17.0-2-armmp')
        dtb_path = self.archive_extract(
            self.ASSET_DEBIAN_LINUX_ARMHF_DEB,
            member='usr/lib/linux-image-5.17.0-2-armmp/aspeed-bmc-ibm-rainier.dtb')

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-net', 'nic')
        self.vm.launch()

        self.wait_for_console_pattern("Booting Linux on physical CPU 0xf00")
        self.wait_for_console_pattern("SMP: Total of 2 processors activated")
        self.wait_for_console_pattern("No filesystem could mount root")


if __name__ == '__main__':
    AspeedTest.main()
