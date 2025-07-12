#!/usr/bin/env python3
#
# Basic validation of x86 versioned CPU models and CPU model aliases
#
#  Copyright (c) 2019 Red Hat Inc
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

import re

from qemu_test import QemuSystemTest

class X86CPUModelAliases(QemuSystemTest):
    """
    Validation of PC CPU model versions and CPU model aliases
    """
    def validate_aliases(self, cpus):
        for c in cpus.values():
            if 'alias-of' in c:
                # all aliases must point to a valid CPU model name:
                self.assertIn(c['alias-of'], cpus,
                              '%s.alias-of (%s) is not a valid CPU model name' % (c['name'], c['alias-of']))
                # aliases must not point to aliases
                self.assertNotIn('alias-of', cpus[c['alias-of']],
                                 '%s.alias-of (%s) points to another alias' % (c['name'], c['alias-of']))

                # aliases must not be static
                self.assertFalse(c['static'])

    def validate_variant_aliases(self, cpus):
        # -noTSX, -IBRS and -IBPB variants of CPU models are special:
        # they shouldn't have their own versions:
        self.assertNotIn("Haswell-noTSX-v1", cpus,
                         "Haswell-noTSX shouldn't be versioned")
        self.assertNotIn("Broadwell-noTSX-v1", cpus,
                         "Broadwell-noTSX shouldn't be versioned")
        self.assertNotIn("Nehalem-IBRS-v1", cpus,
                         "Nehalem-IBRS shouldn't be versioned")
        self.assertNotIn("Westmere-IBRS-v1", cpus,
                         "Westmere-IBRS shouldn't be versioned")
        self.assertNotIn("SandyBridge-IBRS-v1", cpus,
                         "SandyBridge-IBRS shouldn't be versioned")
        self.assertNotIn("IvyBridge-IBRS-v1", cpus,
                         "IvyBridge-IBRS shouldn't be versioned")
        self.assertNotIn("Haswell-noTSX-IBRS-v1", cpus,
                         "Haswell-noTSX-IBRS shouldn't be versioned")
        self.assertNotIn("Haswell-IBRS-v1", cpus,
                         "Haswell-IBRS shouldn't be versioned")
        self.assertNotIn("Broadwell-noTSX-IBRS-v1", cpus,
                         "Broadwell-noTSX-IBRS shouldn't be versioned")
        self.assertNotIn("Broadwell-IBRS-v1", cpus,
                         "Broadwell-IBRS shouldn't be versioned")
        self.assertNotIn("Skylake-Client-IBRS-v1", cpus,
                         "Skylake-Client-IBRS shouldn't be versioned")
        self.assertNotIn("Skylake-Server-IBRS-v1", cpus,
                         "Skylake-Server-IBRS shouldn't be versioned")
        self.assertNotIn("EPYC-IBPB-v1", cpus,
                         "EPYC-IBPB shouldn't be versioned")

    def test_unversioned_alias(self):
        """
        Check if unversioned CPU model is an alias pointing to right version
        """
        self.set_machine('pc')
        self.vm.add_args('-S')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in
                    self.vm.cmd('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertEqual(cpus['Cascadelake-Server'].get('alias-of'),
                         'Cascadelake-Server-v1',
                         'Cascadelake-Server must be an alias of Cascadelake-Server-v1')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-v1'],
                         'Cascadelake-Server-v1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertEqual(cpus['qemu64'].get('alias-of'), 'qemu64-v1',
                         'qemu64 must be an alias of qemu64-v1')
        self.assertNotIn('alias-of', cpus['qemu64-v1'],
                         'qemu64-v1 must not be an alias')

        self.validate_variant_aliases(cpus)

        # On recent PC machines, -noTSX and -IBRS models should be aliases:
        self.assertEqual(cpus["Haswell"].get('alias-of'),
                         "Haswell-v1",
                         "Haswell must be an alias")
        self.assertEqual(cpus["Haswell-noTSX"].get('alias-of'),
                         "Haswell-v2",
                         "Haswell-noTSX must be an alias")
        self.assertEqual(cpus["Haswell-IBRS"].get('alias-of'),
                         "Haswell-v3",
                         "Haswell-IBRS must be an alias")
        self.assertEqual(cpus["Haswell-noTSX-IBRS"].get('alias-of'),
                         "Haswell-v4",
                         "Haswell-noTSX-IBRS must be an alias")

        self.assertEqual(cpus["Broadwell"].get('alias-of'),
                         "Broadwell-v1",
                         "Broadwell must be an alias")
        self.assertEqual(cpus["Broadwell-noTSX"].get('alias-of'),
                         "Broadwell-v2",
                         "Broadwell-noTSX must be an alias")
        self.assertEqual(cpus["Broadwell-IBRS"].get('alias-of'),
                         "Broadwell-v3",
                         "Broadwell-IBRS must be an alias")
        self.assertEqual(cpus["Broadwell-noTSX-IBRS"].get('alias-of'),
                         "Broadwell-v4",
                         "Broadwell-noTSX-IBRS must be an alias")

        self.assertEqual(cpus["Nehalem"].get('alias-of'),
                         "Nehalem-v1",
                         "Nehalem must be an alias")
        self.assertEqual(cpus["Nehalem-IBRS"].get('alias-of'),
                         "Nehalem-v2",
                         "Nehalem-IBRS must be an alias")

        self.assertEqual(cpus["Westmere"].get('alias-of'),
                         "Westmere-v1",
                         "Westmere must be an alias")
        self.assertEqual(cpus["Westmere-IBRS"].get('alias-of'),
                         "Westmere-v2",
                         "Westmere-IBRS must be an alias")

        self.assertEqual(cpus["SandyBridge"].get('alias-of'),
                         "SandyBridge-v1",
                         "SandyBridge must be an alias")
        self.assertEqual(cpus["SandyBridge-IBRS"].get('alias-of'),
                         "SandyBridge-v2",
                         "SandyBridge-IBRS must be an alias")

        self.assertEqual(cpus["IvyBridge"].get('alias-of'),
                         "IvyBridge-v1",
                         "IvyBridge must be an alias")
        self.assertEqual(cpus["IvyBridge-IBRS"].get('alias-of'),
                         "IvyBridge-v2",
                         "IvyBridge-IBRS must be an alias")

        self.assertEqual(cpus["Skylake-Client"].get('alias-of'),
                         "Skylake-Client-v1",
                         "Skylake-Client must be an alias")
        self.assertEqual(cpus["Skylake-Client-IBRS"].get('alias-of'),
                         "Skylake-Client-v2",
                         "Skylake-Client-IBRS must be an alias")

        self.assertEqual(cpus["Skylake-Server"].get('alias-of'),
                         "Skylake-Server-v1",
                         "Skylake-Server must be an alias")
        self.assertEqual(cpus["Skylake-Server-IBRS"].get('alias-of'),
                         "Skylake-Server-v2",
                         "Skylake-Server-IBRS must be an alias")

        self.assertEqual(cpus["EPYC"].get('alias-of'),
                         "EPYC-v1",
                         "EPYC must be an alias")
        self.assertEqual(cpus["EPYC-IBPB"].get('alias-of'),
                         "EPYC-v2",
                         "EPYC-IBPB must be an alias")

        self.validate_aliases(cpus)

    def test_none_alias(self):
        """
        Check if unversioned CPU model is an alias pointing to some version
        """
        self.set_machine('none')
        self.vm.add_args('-S')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in
                    self.vm.cmd('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertTrue(re.match('Cascadelake-Server-v[0-9]+', cpus['Cascadelake-Server']['alias-of']),
                        'Cascadelake-Server must be an alias of versioned CPU model')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-v1'],
                         'Cascadelake-Server-v1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertTrue(re.match('qemu64-v[0-9]+', cpus['qemu64']['alias-of']),
                        'qemu64 must be an alias of versioned CPU model')
        self.assertNotIn('alias-of', cpus['qemu64-v1'],
                         'qemu64-v1 must not be an alias')

        self.validate_aliases(cpus)


class CascadelakeArchCapabilities(QemuSystemTest):
    """
    Validation of Cascadelake arch-capabilities
    """
    def get_cpu_prop(self, prop):
        cpu_path = self.vm.cmd('query-cpus-fast')[0].get('qom-path')
        return self.vm.cmd('qom-get', path=cpu_path, property=prop)

    def test(self):
        self.set_machine('pc')
        # machine-type only:
        self.vm.add_args('-S')
        self.set_vm_arg('-cpu',
                        'Cascadelake-Server,x-force-features=on,check=off,'
                        'enforce=off')
        self.vm.launch()
        self.assertFalse(self.get_cpu_prop('arch-capabilities'),
                         'pc + Cascadelake-Server should not have arch-capabilities')

    def test_unset(self):
        self.set_machine('pc')
        self.vm.add_args('-S')
        self.set_vm_arg('-cpu',
                        'Cascadelake-Server,x-force-features=on,check=off,'
                        'enforce=off,-arch-capabilities')
        self.vm.launch()
        self.assertFalse(self.get_cpu_prop('arch-capabilities'),
                         'pc + Cascadelake-Server,-arch-capabilities should not have arch-capabilities')

    def test_v2_unset(self):
        self.set_machine('pc')
        self.vm.add_args('-S')
        self.set_vm_arg('-cpu',
                        'Cascadelake-Server-v2,x-force-features=on,check=off,'
                        'enforce=off,-arch-capabilities')
        self.vm.launch()
        self.assertFalse(self.get_cpu_prop('arch-capabilities'),
                         'pc + Cascadelake-Server-v2,-arch-capabilities should not have arch-capabilities')

if __name__ == '__main__':
    QemuSystemTest.main()
