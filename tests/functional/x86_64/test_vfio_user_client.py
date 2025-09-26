#!/usr/bin/env python3
#
# Copyright (c) 2025 Nutanix, Inc.
#
# Author:
#  Mark Cave-Ayland <mark.caveayland@nutanix.com>
#  John Levon <john.levon@nutanix.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Check basic vfio-user-pci client functionality. The test starts two VMs:

    - the server VM runs the libvfio-user "gpio" example server inside it,
      piping vfio-user traffic between a local UNIX socket and a virtio-serial
      port. On the host, the virtio-serial port is backed by a local socket.

    - the client VM loads the gpio-pci-idio-16 kernel module, with the
      vfio-user client connecting to the above local UNIX socket.

This way, we don't depend on trying to run a vfio-user server on the host
itself.

Once both VMs are running, we run some basic configuration on the gpio device
and verify that the server is logging the expected out. As this is consistent
given the same VM images, we just do a simple direct comparison.
"""

import os

from qemu_test import Asset
from qemu_test import QemuSystemTest
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

# Exact output can vary, so we just sample for some expected lines.
EXPECTED_SERVER_LINES = [
    "gpio: adding DMA region [0, 0xc0000) offset=0 flags=0x3",
    "gpio: devinfo flags 0x3, num_regions 9, num_irqs 5",
    "gpio: region_info[0] offset 0 flags 0 size 0 argsz 32",
    "gpio: region_info[1] offset 0 flags 0 size 0 argsz 32",
    "gpio: region_info[2] offset 0 flags 0x3 size 256 argsz 32",
    "gpio: region_info[3] offset 0 flags 0 size 0 argsz 32",
    "gpio: region_info[4] offset 0 flags 0 size 0 argsz 32",
    "gpio: region_info[5] offset 0 flags 0 size 0 argsz 32",
    "gpio: region_info[7] offset 0 flags 0x3 size 256 argsz 32",
    "gpio: region7: read 256 bytes at 0",
    "gpio: region7: read 0 from (0x30:4)",
    "gpio: cleared EROM",
    "gpio: I/O space enabled",
    "gpio: memory space enabled",
    "gpio: SERR# enabled",
    "gpio: region7: wrote 0x103 to (0x4:2)",
    "gpio: I/O space enabled",
    "gpio: memory space enabled",
]

class VfioUserClient(QemuSystemTest):
    """vfio-user testing class."""

    ASSET_REPO = 'https://github.com/mcayland-ntx/libvfio-user-test'

    ASSET_KERNEL = Asset(
        f'{ASSET_REPO}/raw/refs/heads/main/images/bzImage',
        '40292fa6ce95d516e26bccf5974e138d0db65a6de0bc540cabae060fe9dea605'
    )

    ASSET_ROOTFS = Asset(
        f'{ASSET_REPO}/raw/refs/heads/main/images/rootfs.ext2',
        'e1e3abae8aebb8e6e77f08b1c531caeacf46250c94c815655c6bbea59fc3d1c1'
    )

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.kernel_path = None
        self.rootfs_path = None

    def configure_server_vm_args(self, server_vm, sock_path):
        """
        Configuration for the server VM. Set up virtio-serial device backed by
        the given socket path.
        """
        server_vm.add_args('-kernel', self.kernel_path)
        server_vm.add_args('-append', 'console=ttyS0 root=/dev/sda')
        server_vm.add_args('-drive',
            f"file={self.rootfs_path},if=ide,format=raw,id=drv0")
        server_vm.add_args('-snapshot')
        server_vm.add_args('-chardev',
            f"socket,id=sock0,path={sock_path},telnet=off,server=on,wait=off")
        server_vm.add_args('-device', 'virtio-serial')
        server_vm.add_args('-device',
            'virtserialport,chardev=sock0,name=org.fedoraproject.port.0')

    def configure_client_vm_args(self, client_vm, sock_path):
        """
        Configuration for the client VM. Point the vfio-user-pci device to the
        socket path configured above.
        """

        client_vm.add_args('-kernel', self.kernel_path)
        client_vm.add_args('-append', 'console=ttyS0 root=/dev/sda')
        client_vm.add_args('-drive',
            f'file={self.rootfs_path},if=ide,format=raw,id=drv0')
        client_vm.add_args('-snapshot')
        client_vm.add_args('-device',
            '{"driver":"vfio-user-pci",' +
            '"socket":{"path": "%s", "type": "unix"}}' % sock_path)

    def setup_vfio_user_pci_server(self, server_vm):
        """
        Start the libvfio-user server within the server VM, and arrange
        for data to shuttle between its socket and the virtio serial port.
        """
        wait_for_console_pattern(self, 'login:', None, server_vm)
        exec_command_and_wait_for_pattern(self, 'root', '#', None, server_vm)

        exec_command_and_wait_for_pattern(self,
            'gpio-pci-idio-16 -v /tmp/vfio-user.sock >/var/tmp/gpio.out 2>&1 &',
            '#', None, server_vm)

        # wait for libvfio-user socket to appear
        while True:
            out = exec_command_and_wait_for_pattern(self,
                 'ls --color=no /tmp/vfio-user.sock', '#', None, server_vm)
            ls_out = out.decode().splitlines()[1].strip()
            if ls_out == "/tmp/vfio-user.sock":
                break

        exec_command_and_wait_for_pattern(self,
            'socat UNIX-CONNECT:/tmp/vfio-user.sock /dev/vport0p1,ignoreeof ' +
            ' &', '#', None, server_vm)

    def test_vfio_user_pci(self):
        """Run basic sanity test."""

        self.set_machine('pc')
        self.require_device('virtio-serial')
        self.require_device('vfio-user-pci')

        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.rootfs_path = self.ASSET_ROOTFS.fetch()

        sock_dir = self.socket_dir()
        socket_path = os.path.join(sock_dir.name, 'vfio-user.sock')

        server_vm = self.get_vm(name='server')
        server_vm.set_console()
        self.configure_server_vm_args(server_vm, socket_path)

        server_vm.launch()

        self.log.debug('starting libvfio-user server')

        self.setup_vfio_user_pci_server(server_vm)

        client_vm = self.get_vm(name="client")
        client_vm.set_console()
        self.configure_client_vm_args(client_vm, socket_path)

        try:
            client_vm.launch()
        except:
            self.log.error('client VM failed to start, dumping server logs')
            exec_command_and_wait_for_pattern(self, 'cat /var/tmp/gpio.out',
                '#', None, server_vm)
            raise

        self.log.debug('waiting for client VM boot')

        wait_for_console_pattern(self, 'login:', None, client_vm)
        exec_command_and_wait_for_pattern(self, 'root', '#', None, client_vm)

        #
        # Here, we'd like to actually interact with the gpio device a little
        # more as described at:
        #
        # https://github.com/nutanix/libvfio-user/blob/master/docs/qemu.md
        #
        # Unfortunately, the buildroot Linux kernel has some undiagnosed issue
        # so we don't get /sys/class/gpio. Nonetheless just the basic
        # initialization and setup is enough for basic testing of vfio-user.
        #

        self.log.debug('collecting libvfio-user server output')

        out = exec_command_and_wait_for_pattern(self,
            'cat /var/tmp/gpio.out',
            'gpio: region2: wrote 0 to (0x1:1)',
            None, server_vm)

        gpio_server_out = [s for s in out.decode().splitlines()
                                   if s.startswith("gpio:")]

        for line in EXPECTED_SERVER_LINES:
            if line not in gpio_server_out:
                self.log.error(f'Missing server debug line: {line}')
                self.fail(False)


if __name__ == '__main__':
    QemuSystemTest.main()
