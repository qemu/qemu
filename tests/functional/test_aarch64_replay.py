#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an aarch64 machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from subprocess import check_call, DEVNULL

from qemu_test import Asset, skipIfOperatingSystem, get_qemu_img
from replay_kernel import ReplayKernelBase


class Aarch64Replay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64/Image',
        'b74743c5e89e1cea0f73368d24ae0ae85c5204ff84be3b5e9610417417d2f235')

    ASSET_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64/rootfs.ext4.zst',
        'a1acaaae2068df4648d04ff75f532aaa8c5edcd6b936122b6f0db4848a07b465')

    def test_aarch64_virt(self):
        self.require_netdev('user')
        self.set_machine('virt')
        self.cpu = 'cortex-a57'
        kernel_path = self.ASSET_KERNEL.fetch()

        raw_disk = self.uncompress(self.ASSET_ROOTFS)
        disk = self.scratch_file('scratch.qcow2')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'qcow2', '-b', raw_disk,
                    '-F', 'raw', disk], stdout=DEVNULL, stderr=DEVNULL)

        args = ('-drive', 'file=%s,snapshot=on,id=hd0,if=none' % disk,
                '-drive', 'driver=blkreplay,id=hd0-rr,if=none,image=hd0',
                '-device', 'virtio-blk-device,drive=hd0-rr',
                '-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                '-device', 'virtio-net,netdev=vnet',
                '-object', 'filter-replay,id=replay,netdev=vnet')

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0 root=/dev/vda')
        console_pattern = 'Welcome to TuxTest'
        self.run_rr(kernel_path, kernel_command_line, console_pattern,
                    args=args)


if __name__ == '__main__':
    ReplayKernelBase.main()
