# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado.utils import archive


class AST1030Machine(QemuSystemTest):
    """Boots the zephyr os and checks that the console is operational"""

    timeout = 10

    def test_ast1030_zephyros(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast1030-evb
        """
        tar_url = ('https://github.com/AspeedTech-BMC'
                   '/zephyr/releases/download/v00.01.04/ast1030-evb-demo.zip')
        tar_hash = '4c6a8ce3a8ba76ef1a65dae419ae3409343c4b20'
        tar_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(tar_path, self.workdir)
        kernel_file = self.workdir + "/ast1030-evb-demo/zephyr.elf"
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file,
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self, "Booting Zephyr OS")
        exec_command_and_wait_for_pattern(self, "help",
                                          "Available commands")
