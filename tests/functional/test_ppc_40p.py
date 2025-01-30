#!/usr/bin/env python3
#
# Functional test that boots a PReP/40p machine and checks its serial console.
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, skipUntrustedTest
from qemu_test import exec_command_and_wait_for_pattern


class IbmPrep40pMachine(QemuSystemTest):

    timeout = 60

    ASSET_BIOS = Asset(
        ('http://ftpmirror.your.org/pub/misc/'
         'ftp.software.ibm.com/rs6000/firmware/'
         '7020-40p/P12H0456.IMG'),
        'd957f79c73f760d1455d2286fcd901ed6d06167320eb73511b478a939be25b3f')
    ASSET_NETBSD40 = Asset(
        ('https://archive.netbsd.org/pub/NetBSD-archive/'
         'NetBSD-4.0/prep/installation/floppy/generic_com0.fs'),
        'f86236e9d01b3f0dd0f5d3b8d5bbd40c68e78b4db560a108358f5ad58e636619')
    ASSET_NETBSD71 = Asset(
        ('https://archive.netbsd.org/pub/NetBSD-archive/'
         'NetBSD-7.1.2/iso/NetBSD-7.1.2-prep.iso'),
        'cc7cb290b06aaa839362deb7bd9f417ac5015557db24088508330f76c3f825ec')

    # 12H0455 PPS Firmware Licensed Materials
    # Property of IBM (C) Copyright IBM Corp. 1994.
    # All rights reserved.
    # U.S. Government Users Restricted Rights - Use, duplication or disclosure
    # restricted by GSA ADP Schedule Contract with IBM Corp.
    @skipUntrustedTest()
    def test_factory_firmware_and_netbsd(self):
        self.set_machine('40p')
        self.require_accelerator("tcg")
        bios_path = self.ASSET_BIOS.fetch()
        drive_path = self.ASSET_NETBSD40.fetch()

        self.vm.set_console()
        self.vm.add_args('-bios', bios_path,
                         '-drive',
                         f"file={drive_path},format=raw,if=floppy,read-only=true")
        self.vm.launch()
        os_banner = 'NetBSD 4.0 (GENERIC) #0: Sun Dec 16 00:49:40 PST 2007'
        wait_for_console_pattern(self, os_banner)
        wait_for_console_pattern(self, 'Model: IBM PPS Model 6015')

    def test_openbios_192m(self):
        self.set_machine('40p')
        self.require_accelerator("tcg")
        self.vm.set_console()
        self.vm.add_args('-m', '192') # test fw_cfg

        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> Memory: 192M')
        wait_for_console_pattern(self, '>> CPU type PowerPC,604')

    def test_openbios_and_netbsd(self):
        self.set_machine('40p')
        self.require_accelerator("tcg")
        drive_path = self.ASSET_NETBSD71.fetch()
        self.vm.set_console()
        self.vm.add_args('-cdrom', drive_path,
                         '-boot', 'd')

        self.vm.launch()
        wait_for_console_pattern(self, 'NetBSD/prep BOOT, Revision 1.9')

    ASSET_40P_SANDALFOOT = Asset(
        'http://www.juneau-lug.org/zImage.initrd.sandalfoot',
        '749ab02f576c6dc8f33b9fb022ecb44bf6a35a0472f2ea6a5e9956bc15933901')

    def test_openbios_and_linux(self):
        self.set_machine('40p')
        self.require_accelerator("tcg")
        drive_path = self.ASSET_40P_SANDALFOOT.fetch()
        self.vm.set_console()
        self.vm.add_args('-cdrom', drive_path,
                         '-boot', 'd')

        self.vm.launch()
        wait_for_console_pattern(self, 'Please press Enter to activate this console.')
        exec_command_and_wait_for_pattern(self, '\012', '#')
        exec_command_and_wait_for_pattern(self, 'uname -a', 'Linux ppc 2.4.18')

if __name__ == '__main__':
    QemuSystemTest.main()
