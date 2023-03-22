# ethtool tests for emulated network devices
#
# This test leverages ethtool's --test sequence to validate network
# device behaviour.
#
# SPDX-License-Identifier: GPL-2.0-or-late

from avocado import skip
from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern

class NetDevEthtool(QemuSystemTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=machine:q35
    """

    # Runs in about 17s under KVM, 19s under TCG, 25s under GCOV
    timeout = 45

    # Fetch assets from the netdev-ethtool subdir of my shared test
    # images directory on fileserver.linaro.org.
    def get_asset(self, name, sha1):
        base_url = ('https://fileserver.linaro.org/s/'
                    'kE4nCFLdQcoBF9t/download?'
                    'path=%2Fnetdev-ethtool&files=' )
        url = base_url + name
        # use explicit name rather than failing to neatly parse the
        # URL into a unique one
        return self.fetch_asset(name=name, locations=(url), asset_hash=sha1)

    def common_test_code(self, netdev, extra_args=None, kvm=False):

        # This custom kernel has drivers for all the supported network
        # devices we can emulate in QEMU
        kernel = self.get_asset("bzImage",
                                "33469d7802732d5815226166581442395cb289e2")

        rootfs = self.get_asset("rootfs.squashfs",
                                "9793cea7021414ae844bda51f558bd6565b50cdc")

        append = 'printk.time=0 console=ttyS0 '
        append += 'root=/dev/sr0 rootfstype=squashfs '

        # any additional kernel tweaks for the test
        if extra_args:
            append += extra_args

        # finally invoke ethtool directly
        append += ' init=/usr/sbin/ethtool -- -t eth1 offline'

        # add the rootfs via a readonly cdrom image
        drive = f"file={rootfs},if=ide,index=0,media=cdrom"

        self.vm.add_args('-kernel', kernel,
                         '-append', append,
                         '-drive', drive,
                         '-device', netdev)

        if kvm:
            self.vm.add_args('-accel', 'kvm')

        self.vm.set_console(console_index=0)
        self.vm.launch()

        wait_for_console_pattern(self,
                                 "The test result is PASS",
                                 "The test result is FAIL",
                                 vm=None)
        # no need to gracefully shutdown, just finish
        self.vm.kill()

    # Skip testing for MSI for now. Allegedly it was fixed by:
    #   28e96556ba (igb: Allocate MSI-X vector when testing)
    # but I'm seeing oops in the kernel
    @skip("Kernel bug with MSI enabled")
    def test_igb(self):
        """
        :avocado: tags=device:igb
        """
        self.common_test_code("igb")

    def test_igb_nomsi(self):
        """
        :avocado: tags=device:igb
        """
        self.common_test_code("igb", "pci=nomsi")

    def test_igb_nomsi_kvm(self):
        """
        :avocado: tags=device:igb
        """
        self.require_accelerator('kvm')
        self.common_test_code("igb", "pci=nomsi", True)

    # It seems the other popular cards we model in QEMU currently fail
    # the pattern test with:
    #
    #   pattern test failed (reg 0x00178): got 0x00000000 expected 0x00005A5A
    #
    # So for now we skip them.

    @skip("Incomplete reg 0x00178 support")
    def test_e1000(self):
        """
        :avocado: tags=device:e1000
        """
        self.common_test_code("e1000")

    @skip("Incomplete reg 0x00178 support")
    def test_i82550(self):
        """
        :avocado: tags=device:i82550
        """
        self.common_test_code("i82550")
