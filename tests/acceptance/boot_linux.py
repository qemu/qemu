# Functional test that boots a complete Linux system via a cloud image
#
# Copyright (c) 2018-2020 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado_qemu import LinuxTest, BUILD_DIR

from avocado import skipIf


class BootLinuxX8664(LinuxTest):
    """
    :avocado: tags=arch:x86_64
    """

    def test_pc_i440fx_tcg(self):
        """
        :avocado: tags=machine:pc
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.launch_and_wait(set_up_ssh_connection=False)

    def test_pc_i440fx_kvm(self):
        """
        :avocado: tags=machine:pc
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.launch_and_wait(set_up_ssh_connection=False)

    def test_pc_q35_tcg(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.launch_and_wait(set_up_ssh_connection=False)

    def test_pc_q35_kvm(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.launch_and_wait(set_up_ssh_connection=False)


class BootLinuxAarch64(LinuxTest):
    """
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:virt
    :avocado: tags=machine:gic-version=2
    """

    def add_common_args(self):
        self.vm.add_args('-bios',
                         os.path.join(BUILD_DIR, 'pc-bios',
                                      'edk2-aarch64-code.fd'))
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object', 'rng-random,id=rng0,filename=/dev/urandom')

    def test_virt_tcg_gicv2(self):
        """
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:max
        :avocado: tags=device:gicv2
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-machine", "virt,gic-version=2")
        self.add_common_args()
        self.launch_and_wait(set_up_ssh_connection=False)

    def test_virt_tcg_gicv3(self):
        """
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:max
        :avocado: tags=device:gicv3
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-machine", "virt,gic-version=3")
        self.add_common_args()
        self.launch_and_wait(set_up_ssh_connection=False)

    def test_virt_kvm(self):
        """
        :avocado: tags=accel:kvm
        :avocado: tags=cpu:host
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-machine", "virt,gic-version=host")
        self.add_common_args()
        self.launch_and_wait(set_up_ssh_connection=False)


class BootLinuxPPC64(LinuxTest):
    """
    :avocado: tags=arch:ppc64
    """

    def test_pseries_tcg(self):
        """
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.launch_and_wait(set_up_ssh_connection=False)


class BootLinuxS390X(LinuxTest):
    """
    :avocado: tags=arch:s390x
    """

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_s390_ccw_virtio_tcg(self):
        """
        :avocado: tags=machine:s390-ccw-virtio
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg")
        self.launch_and_wait(set_up_ssh_connection=False)
