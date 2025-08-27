#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a realview arm machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, exec_command_and_wait_for_pattern
from qemu_test import Asset


class RealviewMachine(LinuxKernelTest):

    ASSET_REALVIEW_MPCORE = Asset(
        ('https://archive.openwrt.org/chaos_calmer/15.05.1/realview/generic/'
         'openwrt-15.05.1-realview-vmlinux-initramfs.elf'),
        'd3a01037f33e7512d46d50975588d5c3a0e0cbf25f37afab44775c2a2be523e6')

    def test_realview_ep_mpcore(self):
        self.require_netdev('user')
        self.set_machine('realview-eb-mpcore')
        kernel_path = self.ASSET_REALVIEW_MPCORE.fetch()
        self.vm.set_console()
        kernel_param = 'console=ttyAMA0 mem=128M quiet'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_param)
        self.vm.launch()
        self.wait_for_console_pattern('Please press Enter to activate')
        prompt = ':/#'
        exec_command_and_wait_for_pattern(self, '', prompt)
        exec_command_and_wait_for_pattern(self, 'dmesg', kernel_param)
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self,
                ('while ! dmesg | grep "br-lan: port 1(eth0) entered" ;'
                 ' do sleep 1 ; done'),
                'entered forwarding state')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self,
                'while ! ifconfig | grep "10.0.2.15" ; do sleep 1 ; done',
                'addr:10.0.2.15')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self, 'ping -c 1 10.0.2.2',
                                          '1 packets received, 0% packet loss')


if __name__ == '__main__':
    LinuxKernelTest.main()
