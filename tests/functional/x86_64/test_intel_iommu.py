#!/usr/bin/env python3
#
# INTEL_IOMMU Functional tests
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Eric Auger <eric.auger@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern


class IntelIOMMU(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/initrd.img'),
        '277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b')

    ASSET_DISKIMAGE = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Cloud/x86_64/images/Fedora-Cloud-Base-31-1.9.x86_64.qcow2'),
        'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0')

    DEFAULT_KERNEL_PARAMS = ('root=/dev/vda1 console=ttyS0 net.ifnames=0 '
                             'quiet rd.rescue ')
    GUEST_PORT = 8080
    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    kernel_path = None
    initrd_path = None
    kernel_params = None

    def add_common_args(self, path):
        self.vm.add_args('-drive', f'file={path},if=none,id=drv0,snapshot=on')
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1,'
                         'werror=stop,rerror=stop' + self.IOMMU_ADDON)
        self.vm.add_args('-device', 'virtio-gpu-pci' + self.IOMMU_ADDON)

        self.vm.add_args('-netdev',
                         'user,id=n1,hostfwd=tcp:127.0.0.1:0-:%d' %
                         self.GUEST_PORT)
        self.vm.add_args('-device',
                         'virtio-net-pci,netdev=n1' + self.IOMMU_ADDON)

        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')
        self.vm.add_args("-m", "1G")
        self.vm.add_args("-accel", "kvm")

    def common_vm_setup(self):
        self.set_machine('q35')
        self.require_accelerator("kvm")
        self.require_netdev('user')

        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.initrd_path = self.ASSET_INITRD.fetch()
        image_path = self.ASSET_DISKIMAGE.fetch()
        self.add_common_args(image_path)
        self.kernel_params = self.DEFAULT_KERNEL_PARAMS

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.vm.set_console()
        self.vm.launch()
        self.wait_for_console_pattern('Entering emergency mode.')
        prompt = '# '
        self.wait_for_console_pattern(prompt)

        # Copy a file (checked later), umount afterwards to drop disk cache:
        exec_command_and_wait_for_pattern(self, 'mount /dev/vda1 /sysroot',
                                          prompt)
        filename = '/boot/initramfs-5.3.7-301.fc31.x86_64.img'
        exec_command_and_wait_for_pattern(self, (f'cp /sysroot{filename}'
                                                 ' /sysroot/root/data'),
                                          prompt)
        exec_command_and_wait_for_pattern(self, 'umount /sysroot', prompt)

        # Switch from initrd to the cloud image filesystem:
        exec_command_and_wait_for_pattern(self, 'mount /dev/vda1 /sysroot',
                                          prompt)
        exec_command_and_wait_for_pattern(self,
                ('for d in dev proc sys run ; do '
                 'mount -o bind /$d /sysroot/$d ; done'), prompt)
        exec_command_and_wait_for_pattern(self, 'chroot /sysroot', prompt)

        # Checking for IOMMU enablement:
        self.log.info("Checking whether IOMMU has been enabled...")
        exec_command_and_wait_for_pattern(self, 'cat /proc/cmdline',
                                          'intel_iommu=on')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self, 'dmesg | grep DMAR:',
                                          'IOMMU enabled')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self,
                                    'find /sys/kernel/iommu_groups/ -type l',
                                    'devices/0000:00:')
        self.wait_for_console_pattern(prompt)

        # Check hard disk device via sha256sum:
        self.log.info("Checking hard disk...")
        hashsum = '0dc7472f879be70b2f3daae279e3ae47175ffe249691e7d97f47222b65b8a720'
        exec_command_and_wait_for_pattern(self, 'sha256sum ' + filename,
                                          hashsum)
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self, 'sha256sum /root/data',
                                          hashsum)
        self.wait_for_console_pattern(prompt)

        # Check virtio-net via HTTP:
        exec_command_and_wait_for_pattern(self, 'dhclient eth0', prompt)
        self.check_http_download(filename, hashsum, self.GUEST_PORT)

    def test_intel_iommu(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on'
        self.run_and_check()

    def test_intel_iommu_strict(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on,strict'
        self.run_and_check()

    def test_intel_iommu_strict_cm(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on,caching-mode=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on,strict'
        self.run_and_check()

    def test_intel_iommu_pt(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on iommu=pt'
        self.run_and_check()

if __name__ == '__main__':
    LinuxKernelTest.main()
