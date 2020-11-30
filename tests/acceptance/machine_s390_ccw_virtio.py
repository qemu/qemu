# Functional test that boots an s390x Linux guest with ccw and PCI devices
# attached and checks whether the devices are recognized by Linux
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Cornelia Huck <cohuck@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern

class S390CCWVirtioMachine(Test):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    timeout = 120

    def test_s390x_devices(self):

        """
        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """

        kernel_url = ('https://snapshot.debian.org/archive/debian/'
                      '20201126T092837Z/dists/buster/main/installer-s390x/'
                      '20190702+deb10u6/images/generic/kernel.debian')
        kernel_hash = '5821fbee57d6220a067a8b967d24595621aa1eb6'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('https://snapshot.debian.org/archive/debian/'
                      '20201126T092837Z/dists/buster/main/installer-s390x/'
                      '20190702+deb10u6/images/generic/initrd.debian')
        initrd_hash = '81ba09c97bef46e8f4660ac25b4ac0a5be3a94d6'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                              'console=sclp0 root=/dev/ram0 BOOT_DEBUG=3')
        self.vm.add_args('-nographic',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-device', 'virtio-net-ccw,devno=fe.1.1111',
                         '-device',
                         'virtio-rng-ccw,devno=fe.2.0000,max_revision=0',
                         '-device',
                         'virtio-rng-ccw,devno=fe.3.1234,max_revision=2',
                         '-device', 'zpci,uid=5,target=zzz',
                         '-device', 'virtio-net-pci,id=zzz',
                         '-device', 'zpci,uid=0xa,fid=12,target=serial',
                         '-device', 'virtio-serial-pci,id=serial')
        self.vm.launch()

        shell_ready = "sh: can't access tty; job control turned off"
        self.wait_for_console_pattern(shell_ready)
        # first debug shell is too early, we need to wait for device detection
        exec_command_and_wait_for_pattern(self, 'exit', shell_ready)

        ccw_bus_ids="0.1.1111  0.2.0000  0.3.1234"
        pci_bus_ids="0005:00:00.0  000a:00:00.0"
        exec_command_and_wait_for_pattern(self, 'ls /sys/bus/ccw/devices/',
                                          ccw_bus_ids)
        exec_command_and_wait_for_pattern(self, 'ls /sys/bus/pci/devices/',
                                          pci_bus_ids)
        # check that the device at 0.2.0000 is in legacy mode, while the
        # device at 0.3.1234 has the virtio-1 feature bit set
        virtio_rng_features="00000000000000000000000000001100" + \
                            "10000000000000000000000000000000"
        virtio_rng_features_legacy="00000000000000000000000000001100" + \
                                   "00000000000000000000000000000000"
        exec_command_and_wait_for_pattern(self,
                        'cat /sys/bus/ccw/devices/0.2.0000/virtio?/features',
                        virtio_rng_features_legacy)
        exec_command_and_wait_for_pattern(self,
                        'cat /sys/bus/ccw/devices/0.3.1234/virtio?/features',
                        virtio_rng_features)
        # verify that we indeed have virtio-net devices (without having the
        # virtio-net driver handy)
        exec_command_and_wait_for_pattern(self,
                                    'cat /sys/bus/ccw/devices/0.1.1111/cutype',
                                    '3832/01')
        exec_command_and_wait_for_pattern(self,
                    'cat /sys/bus/pci/devices/0005\:00\:00.0/subsystem_vendor',
                    '0x1af4')
        exec_command_and_wait_for_pattern(self,
                    'cat /sys/bus/pci/devices/0005\:00\:00.0/subsystem_device',
                    '0x0001')
        # check fid propagation
        exec_command_and_wait_for_pattern(self,
                        'cat /sys/bus/pci/devices/000a\:00\:00.0/function_id',
                        '0x0000000c')
