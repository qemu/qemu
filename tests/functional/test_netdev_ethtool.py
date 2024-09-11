#!/usr/bin/env python3
#
# ethtool tests for emulated network devices
#
# This test leverages ethtool's --test sequence to validate network
# device behaviour.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from unittest import skip
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern

class NetDevEthtool(QemuSystemTest):

    # Runs in about 17s under KVM, 19s under TCG, 25s under GCOV
    timeout = 45

    # Fetch assets from the netdev-ethtool subdir of my shared test
    # images directory on fileserver.linaro.org.
    ASSET_BASEURL = ('https://fileserver.linaro.org/s/kE4nCFLdQcoBF9t/'
                     'download?path=%2Fnetdev-ethtool&files=')
    ASSET_BZIMAGE = Asset(
        ASSET_BASEURL + "bzImage",
        "ed62ee06ea620b1035747f3f66a5e9fc5d3096b29f75562ada888b04cd1c4baf")
    ASSET_ROOTFS = Asset(
        ASSET_BASEURL + "rootfs.squashfs",
        "8f0207e3c4d40832ae73c1a927e42ca30ccb1e71f047acb6ddb161ba422934e6")

    def common_test_code(self, netdev, extra_args=None):
        self.set_machine('q35')

        # This custom kernel has drivers for all the supported network
        # devices we can emulate in QEMU
        kernel = self.ASSET_BZIMAGE.fetch()
        rootfs = self.ASSET_ROOTFS.fetch()

        append = 'printk.time=0 console=ttyS0 '
        append += 'root=/dev/sr0 rootfstype=squashfs '

        # any additional kernel tweaks for the test
        if extra_args:
            append += extra_args

        # finally invoke ethtool directly
        append += ' init=/usr/sbin/ethtool -- -t eth1 offline'

        # add the rootfs via a readonly cdrom image
        drive = f"file={rootfs},if=ide,index=0,media=cdrom"

        self.vm.add_args('-kernel', kernel,
                         '-append', append,
                         '-drive', drive,
                         '-device', netdev)

        self.vm.set_console(console_index=0)
        self.vm.launch()

        wait_for_console_pattern(self,
                                 "The test result is PASS",
                                 "The test result is FAIL",
                                 vm=None)
        # no need to gracefully shutdown, just finish
        self.vm.kill()

    def test_igb(self):
        self.common_test_code("igb")

    def test_igb_nomsi(self):
        self.common_test_code("igb", "pci=nomsi")

    # It seems the other popular cards we model in QEMU currently fail
    # the pattern test with:
    #
    #   pattern test failed (reg 0x00178): got 0x00000000 expected 0x00005A5A
    #
    # So for now we skip them.

    @skip("Incomplete reg 0x00178 support")
    def test_e1000(self):
        self.common_test_code("e1000")

    @skip("Incomplete reg 0x00178 support")
    def test_i82550(self):
        self.common_test_code("i82550")

if __name__ == '__main__':
    QemuSystemTest.main()
