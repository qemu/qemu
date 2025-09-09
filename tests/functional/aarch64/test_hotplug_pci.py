#!/usr/bin/env python3
#
# The test hotplugs a PCI device and checks it on a Linux guest.
#
# Copyright (c) 2025 Linaro Ltd.
#
# Author:
#  Gustavo Romero <gustavo.romero@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import BUILD_DIR

class HotplugPCI(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://ftp.debian.org/debian/dists/trixie/main/installer-arm64/'
         '20250803/images/netboot/debian-installer/arm64/linux'),
         '93a6e4f9627d759375d28f863437a86a0659e125792a435f8e526dda006b7d5e')

    ASSET_INITRD = Asset(
        ('https://ftp.debian.org/debian/dists/trixie/main/installer-arm64/'
         '20250803/images/netboot/debian-installer/arm64/initrd.gz'),
         'f6c78af7078ca67638ef3a50c926cd3c1485673243f8b37952e6bd854d6ba007')

    def test_hotplug_pci(self):

        self.set_machine('virt')

        self.vm.add_args('-m', '512M',
                         '-cpu', 'cortex-a57',
                         '-append',
                         'console=ttyAMA0,115200 init=/bin/sh',
                         '-device',
                         'pcie-root-port,bus=pcie.0,chassis=1,slot=1,id=pcie.1',
                         '-bios',
                         self.build_file('pc-bios', 'edk2-aarch64-code.fd'))

        # BusyBox prompt
        prompt = "~ #"
        self.launch_kernel(self.ASSET_KERNEL.fetch(),
                           self.ASSET_INITRD.fetch(),
                           wait_for=prompt)

        # Check for initial state: 2 network adapters, lo and enp0s1.
        exec_command_and_wait_for_pattern(self,
                                          'ls /sys/class/net | wc -l',
                                          '2')

        # Hotplug one network adapter to the root port, i.e. pcie.1 bus.
        self.vm.cmd('device_add',
                    driver='virtio-net-pci',
                    bus='pcie.1',
                    addr=0,
                    id='na')
        # Wait for the kernel to recognize the new device.
        self.wait_for_console_pattern('virtio-pci')
        self.wait_for_console_pattern('virtio_net')

        # Check if there is a new network adapter.
        exec_command_and_wait_for_pattern(self,
                                          'ls /sys/class/net | wc -l',
                                          '3')

        self.vm.cmd('device_del', id='na')
        exec_command_and_wait_for_pattern(self,
                                          'ls /sys/class/net | wc -l',
                                          '2')

if __name__ == '__main__':
    LinuxKernelTest.main()
