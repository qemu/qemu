#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# SMMUv3 Functional tests
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Eric Auger <eric.auger@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import time

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import BUILD_DIR
from qemu.utils import kvm_available


class SMMU(LinuxKernelTest):

    default_kernel_params = ('earlyprintk=pl011,0x9000000 no_timer_check '
                             'printk.time=1 rd_NO_PLYMOUTH net.ifnames=0 '
                             'console=ttyAMA0 rd.rescue')
    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    kernel_path = None
    initrd_path = None
    kernel_params = None

    GUEST_PORT = 8080

    def set_up_boot(self, path):
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1,'
                         'werror=stop,rerror=stop' + self.IOMMU_ADDON)
        self.vm.add_args('-drive',
                f'file={path},if=none,cache=writethrough,id=drv0,snapshot=on')

        self.vm.add_args('-netdev',
                         'user,id=n1,hostfwd=tcp:127.0.0.1:0-:%d' %
                         self.GUEST_PORT)
        self.vm.add_args('-device', 'virtio-net,netdev=n1' + self.IOMMU_ADDON)

    def common_vm_setup(self, kernel, initrd, disk):
        self.require_accelerator("kvm")
        self.require_netdev('user')
        self.set_machine("virt")
        self.vm.add_args('-m', '1G')
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-cpu", "host")
        self.vm.add_args("-machine", "iommu=smmuv3")
        self.vm.add_args("-d", "guest_errors")
        self.vm.add_args('-bios', os.path.join(BUILD_DIR, 'pc-bios',
                         'edk2-aarch64-code.fd'))
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')

        self.kernel_path = kernel.fetch()
        self.initrd_path = initrd.fetch()
        self.set_up_boot(disk.fetch())

    def run_and_check(self, filename, hashsum):
        self.vm.add_args('-initrd', self.initrd_path)
        self.vm.add_args('-append', self.kernel_params)
        self.launch_kernel(self.kernel_path, initrd=self.initrd_path,
                           wait_for='attach it to a bug report.')
        prompt = '# '
        # Fedora 33 requires 'return' to be pressed to enter the shell.
        # There seems to be a small race between detecting the previous ':'
        # and sending the newline, so we need to add a small delay here.
        self.wait_for_console_pattern(':')
        time.sleep(0.2)
        exec_command_and_wait_for_pattern(self, '\n', prompt)
        exec_command_and_wait_for_pattern(self, 'cat /proc/cmdline',
                                          self.kernel_params)

        # Checking for SMMU enablement:
        self.log.info("Checking whether SMMU has been enabled...")
        exec_command_and_wait_for_pattern(self, 'dmesg | grep smmu',
                                          'arm-smmu-v3')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self,
                                    'find /sys/kernel/iommu_groups/ -type l',
                                    'devices/0000:00:')
        self.wait_for_console_pattern(prompt)

        # Copy a file (checked later), umount afterwards to drop disk cache:
        self.log.info("Checking hard disk...")
        exec_command_and_wait_for_pattern(self,
                        "while ! (dmesg -c | grep vda:) ; do sleep 1 ; done",
                        "vda2")
        exec_command_and_wait_for_pattern(self, 'mount /dev/vda2 /sysroot',
                                          'mounted filesystem')
        exec_command_and_wait_for_pattern(self, 'cp /bin/vi /sysroot/root/vi',
                                          prompt)
        exec_command_and_wait_for_pattern(self, 'umount /sysroot', prompt)
        # Switch from initrd to the cloud image filesystem:
        exec_command_and_wait_for_pattern(self, 'mount /dev/vda2 /sysroot',
                                          prompt)
        exec_command_and_wait_for_pattern(self,
                ('for d in dev proc sys run ; do '
                 'mount -o bind /$d /sysroot/$d ; done'), prompt)
        exec_command_and_wait_for_pattern(self, 'chroot /sysroot', prompt)
        # Check files on the hard disk:
        exec_command_and_wait_for_pattern(self,
            ('if diff -q /root/vi /usr/bin/vi ; then echo "file" "ok" ; '
             'else echo "files differ"; fi'), 'file ok')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self, f'sha256sum {filename}',
                                          hashsum)

        # Check virtio-net via HTTP:
        exec_command_and_wait_for_pattern(self, 'dhclient eth0', prompt)
        self.check_http_download(filename, hashsum, self.GUEST_PORT)


    # 5.3 kernel without RIL #

    ASSET_KERNEL_F31 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/31/Server/aarch64/os/images/pxeboot/vmlinuz'),
        '3ae07fcafbfc8e4abeb693035a74fe10698faae15e9ccd48882a9167800c1527')

    ASSET_INITRD_F31 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/31/Server/aarch64/os/images/pxeboot/initrd.img'),
        '9f3146b28bc531c689f3c5f114cb74e4bd7bd548e0ba19fa77921d8bd256755a')

    ASSET_DISK_F31 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Cloud/aarch64/images/Fedora-Cloud-Base-31-1.9.aarch64.qcow2'),
        '1e18d9c0cf734940c4b5d5ec592facaed2af0ad0329383d5639c997fdf16fe49')

    F31_FILENAME = '/boot/initramfs-5.3.7-301.fc31.aarch64.img'
    F31_HSUM = '1a4beec6607d94df73d9dd1b4985c9c23dd0fdcf4e6ca1351d477f190df7bef9'

    def test_smmu_noril(self):
        self.common_vm_setup(self.ASSET_KERNEL_F31, self.ASSET_INITRD_F31,
                             self.ASSET_DISK_F31)
        self.kernel_params = self.default_kernel_params
        self.run_and_check(self.F31_FILENAME, self.F31_HSUM)

    def test_smmu_noril_passthrough(self):
        self.common_vm_setup(self.ASSET_KERNEL_F31, self.ASSET_INITRD_F31,
                             self.ASSET_DISK_F31)
        self.kernel_params = (self.default_kernel_params +
                              ' iommu.passthrough=on')
        self.run_and_check(self.F31_FILENAME, self.F31_HSUM)

    def test_smmu_noril_nostrict(self):
        self.common_vm_setup(self.ASSET_KERNEL_F31, self.ASSET_INITRD_F31,
                             self.ASSET_DISK_F31)
        self.kernel_params = (self.default_kernel_params +
                              ' iommu.strict=0')
        self.run_and_check(self.F31_FILENAME, self.F31_HSUM)


    # 5.8 kernel featuring range invalidation
    # >= v5.7 kernel

    ASSET_KERNEL_F33 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/33/Server/aarch64/os/images/pxeboot/vmlinuz'),
        'd8b1e6f7241f339d8e7609c456cf0461ffa4583ed07e0b55c7d1d8a0c154aa89')

    ASSET_INITRD_F33 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/33/Server/aarch64/os/images/pxeboot/initrd.img'),
        '92513f55295c2c16a777f7b6c35ccd70a438e9e1e40b6ba39e0e60900615b3df')

    ASSET_DISK_F33 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/33/Cloud/aarch64/images/Fedora-Cloud-Base-33-1.2.aarch64.qcow2'),
        'e7f75cdfd523fe5ac2ca9eeece68edc1a81f386a17f969c1d1c7c87031008a6b')

    F33_FILENAME = '/boot/initramfs-5.8.15-301.fc33.aarch64.img'
    F33_HSUM = '079cfad0caa82e84c8ca1fb0897a4999dd769f262216099f518619e807a550d9'

    def test_smmu_ril(self):
        self.common_vm_setup(self.ASSET_KERNEL_F33, self.ASSET_INITRD_F33,
                             self.ASSET_DISK_F33)
        self.kernel_params = self.default_kernel_params
        self.run_and_check(self.F33_FILENAME, self.F33_HSUM)

    def test_smmu_ril_passthrough(self):
        self.common_vm_setup(self.ASSET_KERNEL_F33, self.ASSET_INITRD_F33,
                             self.ASSET_DISK_F33)
        self.kernel_params = (self.default_kernel_params +
                              ' iommu.passthrough=on')
        self.run_and_check(self.F33_FILENAME, self.F33_HSUM)

    def test_smmu_ril_nostrict(self):
        self.common_vm_setup(self.ASSET_KERNEL_F33, self.ASSET_INITRD_F33,
                             self.ASSET_DISK_F33)
        self.kernel_params = (self.default_kernel_params +
                              ' iommu.strict=0')
        self.run_and_check(self.F33_FILENAME, self.F33_HSUM)


if __name__ == '__main__':
    LinuxKernelTest.main()
