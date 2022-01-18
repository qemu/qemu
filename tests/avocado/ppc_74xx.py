# Smoke tests for 74xx cpus (aka G4).
#
# Copyright (c) 2021, IBM Corp.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class ppc74xxCpu(QemuSystemTest):
    """
    :avocado: tags=arch:ppc
    """
    timeout = 5

    def test_ppc_7400(self):
        """
        :avocado: tags=cpu:7400
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7410(self):
        """
        :avocado: tags=cpu:7410
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,74xx')

    def test_ppc_7441(self):
        """
        :avocado: tags=cpu:7441
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7445(self):
        """
        :avocado: tags=cpu:7445
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7447(self):
        """
        :avocado: tags=cpu:7447
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7447a(self):
        """
        :avocado: tags=cpu:7447a
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7448(self):
        """
        :avocado: tags=cpu:7448
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,MPC86xx')

    def test_ppc_7450(self):
        """
        :avocado: tags=cpu:7450
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7451(self):
        """
        :avocado: tags=cpu:7451
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7455(self):
        """
        :avocado: tags=cpu:7455
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7457(self):
        """
        :avocado: tags=cpu:7457
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7457a(self):
        """
        :avocado: tags=cpu:7457a
        """
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')
