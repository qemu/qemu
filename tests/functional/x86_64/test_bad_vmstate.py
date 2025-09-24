#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
'''Test whether the vmstate-static-checker script detects problems correctly'''

import subprocess

from qemu_test import QemuBaseTest


EXPECTED_OUTPUT='''Warning: checking incompatible machine types: "pc-i440fx-2.1", "pc-i440fx-2.2"
Section "fw_cfg" does not exist in dest
Section "fusbh200-ehci-usb" version error: 2 > 1
Section "fusbh200-ehci-usb", Description "ehci-core": expected field "usbsts", got "usbsts_pending"; skipping rest
Section "pci-serial-4x" Description "pci-serial-multi": Entry "Fields" missing
Section "intel-hda-generic", Description "intel-hda", Field "pci": missing description
Section "cfi.pflash01": Entry "Description" missing
Section "megasas", Description "PCIDevice": expected field "irq_state", while dest has no further fields
Section "PIIX3-xen" Description "PIIX3": minimum version error: 1 < 2
Section "PIIX3-xen" Description "PIIX3": Entry "Subsections" missing
Section "tpci200": Description "tpci200" missing, got "tpci2002" instead; skipping
Section "sun-fdtwo" Description "fdc": version error: 2 > 1
Section "sun-fdtwo", Description "fdrive": Subsection "fdrive/media_rate" not found
Section "usb-kbd" Description "usb-kbd" Field "kbd.keycodes" size mismatch: 4 , 2
'''

class BadVmStateTest(QemuBaseTest):
    '''Test class for testing vmstat-static-checker script with bad input'''

    def test_checker(self):
        """
        Test whether the checker script correctly detects the changes
        between dump1.json and dump2.json.
        """
        src_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  'dump1.json')
        dst_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  'dump2.json')
        checkerscript = self.data_file('..', '..', 'scripts',
                                       'vmstate-static-checker.py')

        self.log.info('Comparing %s with %s', src_json, dst_json)
        cp = subprocess.run([checkerscript, '-s', src_json, '-d', dst_json],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True, check=False)
        if cp.returncode != 13:
            self.fail('Unexpected return code of vmstate-static-checker: ' +
                      cp.returncode)
        if cp.stdout != EXPECTED_OUTPUT:
            self.log.info('vmstate-static-checker output:\n%s', cp.stdout)
            self.log.info('expected output:\n%s', EXPECTED_OUTPUT)
            self.fail('Unexpected vmstate-static-checker output!')


if __name__ == '__main__':
    QemuBaseTest.main()
