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

from avocado import skipUnless
from avocado_qemu import LinuxTest, BUILD_DIR

@skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

class SMMU(LinuxTest):
    """
    :avocado: tags=accel:kvm
    :avocado: tags=cpu:host
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:virt
    :avocado: tags=distro:fedora
    :avocado: tags=smmu
    :avocado: tags=flaky
    """

    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    kernel_path = None
    initrd_path = None
    kernel_params = None

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,scsi=off,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1,'
                         'werror=stop,rerror=stop' + self.IOMMU_ADDON)
        self.vm.add_args('-drive',
                         'file=%s,if=none,cache=writethrough,id=drv0' % path)

    def setUp(self):
        super(SMMU, self).setUp(None, 'virtio-net-pci' + self.IOMMU_ADDON)

    def common_vm_setup(self, custom_kernel=False):
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-cpu", "host")
        self.vm.add_args("-machine", "iommu=smmuv3")
        self.vm.add_args("-d", "guest_errors")
        self.vm.add_args('-bios', os.path.join(BUILD_DIR, 'pc-bios',
                         'edk2-aarch64-code.fd'))
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')

        if custom_kernel is False:
            return

        kernel_url = self.distro.pxeboot_url + 'vmlinuz'
        initrd_url = self.distro.pxeboot_url + 'initrd.img'
        self.kernel_path = self.fetch_asset(kernel_url)
        self.initrd_path = self.fetch_asset(initrd_url)

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.launch_and_wait()
        self.ssh_command('cat /proc/cmdline')
        self.ssh_command('dnf -y install numactl-devel')


    # 5.3 kernel without RIL #

    def test_smmu_noril(self):
        """
        :avocado: tags=smmu_noril
        :avocado: tags=smmu_noril_tests
        :avocado: tags=distro_version:31
        """
        self.common_vm_setup()
        self.run_and_check()

    def test_smmu_noril_passthrough(self):
        """
        :avocado: tags=smmu_noril_passthrough
        :avocado: tags=smmu_noril_tests
        :avocado: tags=distro_version:31
        """
        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' iommu.passthrough=on')
        self.run_and_check()

    def test_smmu_noril_nostrict(self):
        """
        :avocado: tags=smmu_noril_nostrict
        :avocado: tags=smmu_noril_tests
        :avocado: tags=distro_version:31
        """
        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' iommu.strict=0')
        self.run_and_check()

    # 5.8 kernel featuring range invalidation
    # >= v5.7 kernel

    def test_smmu_ril(self):
        """
        :avocado: tags=smmu_ril
        :avocado: tags=smmu_ril_tests
        :avocado: tags=distro_version:33
        """
        self.common_vm_setup()
        self.run_and_check()

    def test_smmu_ril_passthrough(self):
        """
        :avocado: tags=smmu_ril_passthrough
        :avocado: tags=smmu_ril_tests
        :avocado: tags=distro_version:33
        """
        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' iommu.passthrough=on')
        self.run_and_check()

    def test_smmu_ril_nostrict(self):
        """
        :avocado: tags=smmu_ril_nostrict
        :avocado: tags=smmu_ril_tests
        :avocado: tags=distro_version:33
        """
        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' iommu.strict=0')
        self.run_and_check()
