#!/usr/bin/env python3
#
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

import os
import tempfile

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class S390CCWVirtioMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    timeout = 120

    ASSET_BUSTER_KERNEL = Asset(
        ('https://snapshot.debian.org/archive/debian/'
         '20201126T092837Z/dists/buster/main/installer-s390x/'
         '20190702+deb10u6/images/generic/kernel.debian'),
        'd411d17c39ae7ad38d27534376cbe88b68b403c325739364122c2e6f1537e818')
    ASSET_BUSTER_INITRD = Asset(
        ('https://snapshot.debian.org/archive/debian/'
         '20201126T092837Z/dists/buster/main/installer-s390x/'
         '20190702+deb10u6/images/generic/initrd.debian'),
        '836bbd0fe6a5ca81274c28c2b063ea315ce1868660866e9b60180c575fef9fd5')

    ASSET_F31_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive'
         '/fedora-secondary/releases/31/Server/s390x/os'
         '/images/kernel.img'),
        '480859574f3f44caa6cd35c62d70e1ac0609134e22ce2a954bbed9b110c06e0b')
    ASSET_F31_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive'
         '/fedora-secondary/releases/31/Server/s390x/os'
         '/images/initrd.img'),
        '04c46095b2c49020b1c2327158898b7db747e4892ae319726192fb949716aa9c')

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def wait_for_crw_reports(self):
        exec_command_and_wait_for_pattern(self,
                        'while ! (dmesg -c | grep CRW) ; do sleep 1 ; done',
                        'CRW reports')

    dmesg_clear_count = 1
    def clear_guest_dmesg(self):
        exec_command_and_wait_for_pattern(self, 'dmesg -c > /dev/null; '
                    r'echo dm_clear\ ' + str(self.dmesg_clear_count),
                    r'dm_clear ' + str(self.dmesg_clear_count))
        self.dmesg_clear_count += 1

    def test_s390x_devices(self):
        self.set_machine('s390-ccw-virtio')

        kernel_path = self.ASSET_BUSTER_KERNEL.fetch()
        initrd_path = self.ASSET_BUSTER_INITRD.fetch()

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                              'console=sclp0 root=/dev/ram0 BOOT_DEBUG=3')
        self.vm.add_args('-nographic',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-cpu', 'max,prno-trng=off',
                         '-device', 'virtio-net-ccw,devno=fe.1.1111',
                         '-device',
                         'virtio-rng-ccw,devno=fe.2.0000,max_revision=0,id=rn1',
                         '-device',
                         'virtio-rng-ccw,devno=fe.3.1234,max_revision=2,id=rn2',
                         '-device', 'zpci,uid=5,target=zzz',
                         '-device', 'virtio-net-pci,id=zzz',
                         '-device', 'zpci,uid=0xa,fid=12,target=serial',
                         '-device', 'virtio-serial-pci,id=serial',
                         '-device', 'virtio-balloon-ccw')
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
        # check that /dev/hwrng works - and that it's gone after ejecting
        exec_command_and_wait_for_pattern(self,
                        'dd if=/dev/hwrng of=/dev/null bs=1k count=10',
                        '10+0 records out')
        self.clear_guest_dmesg()
        self.vm.cmd('device_del', id='rn1')
        self.wait_for_crw_reports()
        self.clear_guest_dmesg()
        self.vm.cmd('device_del', id='rn2')
        self.wait_for_crw_reports()
        exec_command_and_wait_for_pattern(self,
                        'dd if=/dev/hwrng of=/dev/null bs=1k count=10',
                        'dd: /dev/hwrng: No such device')
        # verify that we indeed have virtio-net devices (without having the
        # virtio-net driver handy)
        exec_command_and_wait_for_pattern(self,
                                    'cat /sys/bus/ccw/devices/0.1.1111/cutype',
                                    '3832/01')
        exec_command_and_wait_for_pattern(self,
                    r'cat /sys/bus/pci/devices/0005\:00\:00.0/subsystem_vendor',
                    r'0x1af4')
        exec_command_and_wait_for_pattern(self,
                    r'cat /sys/bus/pci/devices/0005\:00\:00.0/subsystem_device',
                    r'0x0001')
        # check fid propagation
        exec_command_and_wait_for_pattern(self,
                    r'cat /sys/bus/pci/devices/000a\:00\:00.0/function_id',
                    r'0x0000000c')
        # add another device
        self.clear_guest_dmesg()
        self.vm.cmd('device_add', driver='virtio-net-ccw',
                    devno='fe.0.4711', id='net_4711')
        self.wait_for_crw_reports()
        exec_command_and_wait_for_pattern(self, 'for i in 1 2 3 4 5 6 7 ; do '
                    'if [ -e /sys/bus/ccw/devices/*4711 ]; then break; fi ;'
                    'sleep 1 ; done ; ls /sys/bus/ccw/devices/',
                    '0.0.4711')
        # and detach it again
        self.clear_guest_dmesg()
        self.vm.cmd('device_del', id='net_4711')
        self.vm.event_wait(name='DEVICE_DELETED',
                           match={'data': {'device': 'net_4711'}})
        self.wait_for_crw_reports()
        exec_command_and_wait_for_pattern(self,
                                          'ls /sys/bus/ccw/devices/0.0.4711',
                                          'No such file or directory')
        # test the virtio-balloon device
        exec_command_and_wait_for_pattern(self, 'head -n 1 /proc/meminfo',
                                          'MemTotal:         115640 kB')
        self.vm.cmd('human-monitor-command', command_line='balloon 96')
        exec_command_and_wait_for_pattern(self, 'head -n 1 /proc/meminfo',
                                          'MemTotal:          82872 kB')
        self.vm.cmd('human-monitor-command', command_line='balloon 128')
        exec_command_and_wait_for_pattern(self, 'head -n 1 /proc/meminfo',
                                          'MemTotal:         115640 kB')


    def test_s390x_fedora(self):
        self.set_machine('s390-ccw-virtio')

        kernel_path = self.ASSET_F31_KERNEL.fetch()

        initrd_path = self.uncompress(self.ASSET_F31_INITRD, format="xz")

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE + ' audit=0 '
                              'rd.plymouth=0 plymouth.enable=0 rd.rescue')
        self.vm.add_args('-nographic',
                         '-smp', '4',
                         '-m', '512',
                         '-name', 'Some Guest Name',
                         '-uuid', '30de4fd9-b4d5-409e-86a5-09b387f70bfa',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-device', 'zpci,uid=7,target=n',
                         '-device', 'virtio-net-pci,id=n,mac=02:ca:fe:fa:ce:12',
                         '-device', 'virtio-rng-ccw,devno=fe.1.9876',
                         '-device', 'virtio-gpu-ccw,devno=fe.2.5432')
        self.vm.launch()
        self.wait_for_console_pattern('Kernel command line: %s'
                                      % kernel_command_line)
        self.wait_for_console_pattern('Entering emergency mode')

        # Some tests to see whether the CLI options have been considered:
        self.log.info("Test whether QEMU CLI options have been considered")
        exec_command_and_wait_for_pattern(self,
                        'while ! (dmesg | grep enP7p0s0) ; do sleep 1 ; done',
                        'virtio_net virtio0 enP7p0s0: renamed')
        exec_command_and_wait_for_pattern(self, 'lspci',
                             '0007:00:00.0 Class 0200: Device 1af4:1000')
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/net/enP7p0s0/address',
                             '02:ca:fe:fa:ce:12')
        exec_command_and_wait_for_pattern(self, 'lscss', '0.1.9876')
        exec_command_and_wait_for_pattern(self, 'lscss', '0.2.5432')
        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                             'processors    : 4')
        exec_command_and_wait_for_pattern(self, 'grep MemTotal /proc/meminfo',
                             'MemTotal:         499848 kB')
        exec_command_and_wait_for_pattern(self, 'grep Name /proc/sysinfo',
                             'Extended Name:   Some Guest Name')
        exec_command_and_wait_for_pattern(self, 'grep UUID /proc/sysinfo',
                             '30de4fd9-b4d5-409e-86a5-09b387f70bfa')

        # Disable blinking cursor, then write some stuff into the framebuffer.
        # QEMU's PPM screendumps contain uncompressed 24-bit values, while the
        # framebuffer uses 32-bit, so we pad our text with some spaces when
        # writing to the framebuffer. Since the PPM is uncompressed, we then
        # can simply read the written "magic bytes" back from the PPM file to
        # check whether the framebuffer is working as expected.
        # Unfortunately, this test is flaky, so we don't run it by default
        if os.getenv('QEMU_TEST_FLAKY_TESTS'):
            self.log.info("Test screendump of virtio-gpu device")
            exec_command_and_wait_for_pattern(self,
                        'while ! (dmesg | grep gpudrmfb) ; do sleep 1 ; done',
                        'virtio_gpudrmfb frame buffer device')
            exec_command_and_wait_for_pattern(self,
                r'echo -e "\e[?25l" > /dev/tty0', ':/#')
            exec_command_and_wait_for_pattern(self, 'for ((i=0;i<250;i++)); do '
                'echo " The  qu ick  fo x j ump s o ver  a  laz y d og" >> fox.txt;'
                'done',
                ':/#')
            exec_command_and_wait_for_pattern(self,
                'dd if=fox.txt of=/dev/fb0 bs=1000 oflag=sync,nocache ; rm fox.txt',
                '12+0 records out')
            with tempfile.NamedTemporaryFile(suffix='.ppm',
                                             prefix='qemu-scrdump-') as ppmfile:
                self.vm.cmd('screendump', filename=ppmfile.name)
                ppmfile.seek(0)
                line = ppmfile.readline()
                self.assertEqual(line, b"P6\n")
                line = ppmfile.readline()
                self.assertEqual(line, b"1280 800\n")
                line = ppmfile.readline()
                self.assertEqual(line, b"255\n")
                line = ppmfile.readline(256)
                self.assertEqual(line, b"The quick fox jumps over a lazy dog\n")
        else:
            self.log.info("Skipped flaky screendump of virtio-gpu device test")

        # Hot-plug a virtio-crypto device and see whether it gets accepted
        self.log.info("Test hot-plug virtio-crypto device")
        self.clear_guest_dmesg()
        self.vm.cmd('object-add', qom_type='cryptodev-backend-builtin',
                    id='cbe0')
        self.vm.cmd('device_add', driver='virtio-crypto-ccw', id='crypdev0',
                    cryptodev='cbe0', devno='fe.0.2342')
        exec_command_and_wait_for_pattern(self,
                        'while ! (dmesg -c | grep Accelerator.device) ; do'
                        ' sleep 1 ; done', 'Accelerator device is ready')
        exec_command_and_wait_for_pattern(self, 'lscss', '0.0.2342')
        self.vm.cmd('device_del', id='crypdev0')
        self.vm.cmd('object-del', id='cbe0')
        exec_command_and_wait_for_pattern(self,
                        'while ! (dmesg -c | grep Start.virtcrypto_remove) ; do'
                        ' sleep 1 ; done', 'Start virtcrypto_remove.')

if __name__ == '__main__':
    QemuSystemTest.main()
