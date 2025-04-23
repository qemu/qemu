#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on x86_64 machines
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from subprocess import check_call, DEVNULL

from qemu_test import Asset, skipFlakyTest, get_qemu_img
from replay_kernel import ReplayKernelBase


class X86Replay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/x86_64/bzImage',
        'f57bfc6553bcd6e0a54aab86095bf642b33b5571d14e3af1731b18c87ed5aef8')

    ASSET_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/x86_64/rootfs.ext4.zst',
        '4b8b2a99117519c5290e1202cb36eb6c7aaba92b357b5160f5970cf5fb78a751')

    def do_test_x86(self, machine, blkdevice, devroot):
        self.require_netdev('user')
        self.set_machine(machine)
        self.cpu="Nehalem"
        kernel_path = self.ASSET_KERNEL.fetch()

        raw_disk = self.uncompress(self.ASSET_ROOTFS)
        disk = self.scratch_file('scratch.qcow2')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'qcow2', '-b', raw_disk,
                    '-F', 'raw', disk], stdout=DEVNULL, stderr=DEVNULL)

        args = ('-drive', 'file=%s,snapshot=on,id=hd0,if=none' % disk,
                '-drive', 'driver=blkreplay,id=hd0-rr,if=none,image=hd0',
                '-device', '%s,drive=hd0-rr' % blkdevice,
                '-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                '-device', 'virtio-net,netdev=vnet',
                '-object', 'filter-replay,id=replay,netdev=vnet')

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               f"console=ttyS0 root=/dev/{devroot}")
        console_pattern = 'Welcome to TuxTest'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5,
                    args=args)

    @skipFlakyTest('https://gitlab.com/qemu-project/qemu/-/issues/2094')
    def test_pc(self):
        self.do_test_x86('pc', 'virtio-blk', 'vda')

    def test_q35(self):
        self.do_test_x86('q35', 'ide-hd', 'sda')


if __name__ == '__main__':
    ReplayKernelBase.main()
