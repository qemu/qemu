# Test class to boot aspeed machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import LinuxKernelTest

class AspeedTest(LinuxKernelTest):

    def do_test_arm_aspeed(self, machine, image):
        self.set_machine(machine)
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern("U-Boot 2016.07")
        self.wait_for_console_pattern("## Loading kernel from FIT Image at 20080000")
        self.wait_for_console_pattern("Starting kernel ...")
        self.wait_for_console_pattern("Booting Linux on physical CPU 0x0")
        self.wait_for_console_pattern(
                "aspeed-smc 1e620000.spi: read control register: 203b0641")
        self.wait_for_console_pattern("ftgmac100 1e660000.ethernet eth0: irq ")
        self.wait_for_console_pattern("systemd[1]: Set hostname to")

    def do_test_arm_aspeed_buildroot_start(self, image, cpu_id, pattern='Aspeed EVB'):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw,read-only=true',
                         '-net', 'nic', '-net', 'user')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Booting Linux on physical CPU ' + cpu_id)
        self.wait_for_console_pattern('lease of 10.0.2.15')
        # the line before login:
        self.wait_for_console_pattern(pattern)
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, 'passw0rd', '#')

    def do_test_arm_aspeed_buildroot_poweroff(self):
        exec_command_and_wait_for_pattern(self, 'poweroff',
                                          'System halted');

    def do_test_arm_aspeed_sdk_start(self, image):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
