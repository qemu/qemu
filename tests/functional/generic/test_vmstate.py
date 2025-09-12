#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
'''This test runs the vmstate-static-checker script with the current QEMU'''

import subprocess

from qemu_test import QemuSystemTest, skipFlakyTest


@skipFlakyTest("vmstate-static-checker can produce false positives")
class VmStateTest(QemuSystemTest):
    '''
    This test helps to check whether there are problems between old
    reference data and the current QEMU
    '''

    def test_vmstate_7_2(self):
        '''Check reference data from QEMU v7.2'''

        target_machine = {
            'aarch64': 'virt-7.2',
            'm68k': 'virt-7.2',
            'ppc64': 'pseries-7.2',
            's390x': 's390-ccw-virtio-7.2',
            'x86_64': 'pc-q35-7.2',
        }
        self.set_machine(target_machine[self.arch])

        # Run QEMU to get the current vmstate json file:
        dst_json = self.scratch_file('dest.json')
        self.log.info('Dumping vmstate from %s', self.qemu_bin)
        cp = subprocess.run([self.qemu_bin, '-nodefaults',
                             '-M', target_machine[self.arch],
                             '-dump-vmstate', dst_json],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True, check=True)
        if cp.stdout:
            self.log.info('QEMU output: %s', cp.stdout)

        # Check whether the old vmstate json file is still compatible:
        src_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  self.arch,
                                  target_machine[self.arch] + '.json')
        self.log.info('Comparing vmstate with %s', src_json)
        checkerscript = self.data_file('..', '..', 'scripts',
                                       'vmstate-static-checker.py')
        cp = subprocess.run([checkerscript, '-s', src_json, '-d', dst_json],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True, check=False)
        if cp.returncode != 0:
            self.fail('Running vmstate-static-checker failed:\n' + cp.stdout +
                      '\nThis either means that there is a migration bug '
                      'that needs to be fixed, or\nvmstate-static-checker.py '
                      'needs to be improved (e.g. extend the changed_names\n'
                      'in case a field has been renamed), or drop the '
                      'problematic field from\n' + src_json +
                      '\nin case the script cannot be fixed easily.')
        if cp.stdout:
            self.log.warning('vmstate-static-checker output: %s', cp.stdout)


if __name__ == '__main__':
    QemuSystemTest.main()
