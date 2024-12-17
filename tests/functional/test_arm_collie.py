#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a collie machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class CollieTest(LinuxKernelTest):

    ASSET_ZIMAGE = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/collie/zImage',
        '10ace8abf9e0875ef8a83b8829cc3b5b50bc6d7bc3ca29f19f49f5673a43c13b')

    ASSET_ROOTFS = Asset(
        'https://github.com/groeck/linux-test-downloads/raw/225223f2ad7d637b34426810bf6c3b727b76a718/collie/rootfs-sa110.cpio',
        '89ccaaa5c6b33331887047e1618ffe81b0f55909173944347d5d2426f3bcc1f2')

    def test_arm_collie(self):
        self.set_machine('collie')
        zimage_path = self.ASSET_ZIMAGE.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()
        self.vm.add_args('-append', 'rdinit=/sbin/init console=ttySA1')
        self.launch_kernel(zimage_path,
                           initrd=rootfs_path,
                           wait_for='reboot: Restarting system')

if __name__ == '__main__':
    LinuxKernelTest.main()
