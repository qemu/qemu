# Test class to boot aspeed machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import LinuxKernelTest

class AspeedTest(LinuxKernelTest):

    def do_test_arm_aspeed_openbmc(self, machine, image, uboot='2019.04',
                                   cpu_id='0x0', soc='AST2500 rev A1',
                                   image_hostname=None):
        # Allow for the image hostname to not end in "-bmc"
        if image_hostname is not None:
            hostname = image_hostname
        else:
            hostname = machine.removesuffix('-bmc')

        self.set_machine(machine)
        self.vm.set_console()
        self.vm.add_args('-drive', f'file={image},if=mtd,format=raw',
                         '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern(f'U-Boot {uboot}')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern(f'Booting Linux on physical CPU {cpu_id}')
        self.wait_for_console_pattern(f'ASPEED {soc}')
        self.wait_for_console_pattern('/init as init process')
        self.wait_for_console_pattern(f'systemd[1]: Hostname set to <{hostname}>.')

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
                                          'System halted')

    def do_test_arm_aspeed_sdk_start(self, image):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')

    def generate_otpmem_image(self):
        path = self.scratch_file("otpmem.img")
        pattern = b'\x00\x00\x00\x00\xff\xff\xff\xff' * (16 * 1024 // 8)
        with open(path, "wb") as f:
            f.write(pattern)
        return path

