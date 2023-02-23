# SPDX-License-Identifier: GPL-2.0-or-later
# ethtool tests for igb registers, interrupts, etc

from avocado_qemu import LinuxTest

class IGB(LinuxTest):
    """
    :avocado: tags=accel:kvm
    :avocado: tags=arch:x86_64
    :avocado: tags=distro:fedora
    :avocado: tags=distro_version:31
    :avocado: tags=machine:q35
    """

    timeout = 180

    def test(self):
        self.require_accelerator('kvm')
        kernel_url = self.distro.pxeboot_url + 'vmlinuz'
        kernel_hash = '5b6f6876e1b5bda314f93893271da0d5777b1f3c'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        initrd_url = self.distro.pxeboot_url + 'initrd.img'
        initrd_hash = 'dd0340a1b39bd28f88532babd4581c67649ec5b1'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        # Ideally we want to test MSI as well, but it is blocked by a bug
        # fixed with:
        # https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=28e96556baca7056d11d9fb3cdd0aba4483e00d8
        kernel_params = self.distro.default_kernel_params + ' pci=nomsi'

        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_params,
                         '-accel', 'kvm',
                         '-device', 'igb')
        self.launch_and_wait()
        self.ssh_command('dnf -y install ethtool')
        self.ssh_command('ethtool -t eth1 offline')
