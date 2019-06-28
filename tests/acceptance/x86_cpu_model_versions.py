#!/usr/bin/env python
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
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#


import avocado_qemu
import re

def get_cpu_prop(vm, prop):
    cpu_path = vm.command('query-cpus')[0].get('qom_path')
    return vm.command('qom-get', path=cpu_path, property=prop)

class X86CPUModelAliases(avocado_qemu.Test):
    """
    Validation of PC CPU model versions and CPU model aliases

    :avocado: tags=arch:x86_64
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

    def test_4_0_alias_compatibility(self):
        """Check if pc-*-4.0 unversioned CPU model won't be reported as aliases"""
        # pc-*-4.0 won't expose non-versioned CPU models as aliases
        # We do this to help management software to keep compatibility
        # with older QEMU versions that didn't have the versioned CPU model
        self.vm.add_args('-S')
        self.vm.set_machine('pc-i440fx-4.0')
        self.vm.launch()
        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server'],
                         'Cascadelake-Server must not be an alias')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-v1'],
                         'Cascadelake-Server-v1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertNotIn('alias-of', cpus['qemu64'],
                         'qemu64 must not be an alias')
        self.assertNotIn('alias-of', cpus['qemu64-v1'],
                         'qemu64-v1 must not be an alias')

        self.validate_variant_aliases(cpus)

        # On pc-*-4.0, no CPU model should be reported as an alias:
        for name,c in cpus.items():
            self.assertNotIn('alias-of', c, "%s shouldn't be an alias" % (name))

    def test_4_1_alias(self):
        """Check if unversioned CPU model is an alias pointing to right version"""
        self.vm.add_args('-S')
        self.vm.set_machine('pc-i440fx-4.1')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertEquals(cpus['Cascadelake-Server'].get('alias-of'), 'Cascadelake-Server-v1',
                          'Cascadelake-Server must be an alias of Cascadelake-Server-v1')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-v1'],
                         'Cascadelake-Server-v1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertEquals(cpus['qemu64'].get('alias-of'), 'qemu64-v1',
                          'qemu64 must be an alias of qemu64-v1')
        self.assertNotIn('alias-of', cpus['qemu64-v1'],
                         'qemu64-v1 must not be an alias')

        self.validate_variant_aliases(cpus)

        # On pc-*-4.1, -noTSX and -IBRS models should be aliases:
        self.assertEquals(cpus["Haswell"].get('alias-of'),
                          "Haswell-v1",
                         "Haswell must be an alias")
        self.assertEquals(cpus["Haswell-noTSX"].get('alias-of'),
                          "Haswell-v2",
                         "Haswell-noTSX must be an alias")
        self.assertEquals(cpus["Haswell-IBRS"].get('alias-of'),
                          "Haswell-v3",
                         "Haswell-IBRS must be an alias")
        self.assertEquals(cpus["Haswell-noTSX-IBRS"].get('alias-of'),
                          "Haswell-v4",
                         "Haswell-noTSX-IBRS must be an alias")

        self.assertEquals(cpus["Broadwell"].get('alias-of'),
                          "Broadwell-v1",
                         "Broadwell must be an alias")
        self.assertEquals(cpus["Broadwell-noTSX"].get('alias-of'),
                          "Broadwell-v2",
                         "Broadwell-noTSX must be an alias")
        self.assertEquals(cpus["Broadwell-IBRS"].get('alias-of'),
                          "Broadwell-v3",
                         "Broadwell-IBRS must be an alias")
        self.assertEquals(cpus["Broadwell-noTSX-IBRS"].get('alias-of'),
                          "Broadwell-v4",
                         "Broadwell-noTSX-IBRS must be an alias")

        self.assertEquals(cpus["Nehalem"].get('alias-of'),
                          "Nehalem-v1",
                         "Nehalem must be an alias")
        self.assertEquals(cpus["Nehalem-IBRS"].get('alias-of'),
                          "Nehalem-v2",
                         "Nehalem-IBRS must be an alias")

        self.assertEquals(cpus["Westmere"].get('alias-of'),
                          "Westmere-v1",
                         "Westmere must be an alias")
        self.assertEquals(cpus["Westmere-IBRS"].get('alias-of'),
                          "Westmere-v2",
                         "Westmere-IBRS must be an alias")

        self.assertEquals(cpus["SandyBridge"].get('alias-of'),
                          "SandyBridge-v1",
                         "SandyBridge must be an alias")
        self.assertEquals(cpus["SandyBridge-IBRS"].get('alias-of'),
                          "SandyBridge-v2",
                         "SandyBridge-IBRS must be an alias")

        self.assertEquals(cpus["IvyBridge"].get('alias-of'),
                          "IvyBridge-v1",
                         "IvyBridge must be an alias")
        self.assertEquals(cpus["IvyBridge-IBRS"].get('alias-of'),
                          "IvyBridge-v2",
                         "IvyBridge-IBRS must be an alias")

        self.assertEquals(cpus["Skylake-Client"].get('alias-of'),
                          "Skylake-Client-v1",
                         "Skylake-Client must be an alias")
        self.assertEquals(cpus["Skylake-Client-IBRS"].get('alias-of'),
                          "Skylake-Client-v2",
                         "Skylake-Client-IBRS must be an alias")

        self.assertEquals(cpus["Skylake-Server"].get('alias-of'),
                          "Skylake-Server-v1",
                         "Skylake-Server must be an alias")
        self.assertEquals(cpus["Skylake-Server-IBRS"].get('alias-of'),
                          "Skylake-Server-v2",
                         "Skylake-Server-IBRS must be an alias")

        self.assertEquals(cpus["EPYC"].get('alias-of'),
                          "EPYC-v1",
                         "EPYC must be an alias")
        self.assertEquals(cpus["EPYC-IBPB"].get('alias-of'),
                          "EPYC-v2",
                         "EPYC-IBPB must be an alias")

        self.validate_aliases(cpus)

    def test_none_alias(self):
        """Check if unversioned CPU model is an alias pointing to some version"""
        self.vm.add_args('-S')
        self.vm.set_machine('none')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

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

    def test_Cascadelake_arch_capabilities_result(self):
        # machine-type only:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server should not have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.0 + Cascadelake-Server should not have arch-capabilities')

        # command line must override machine-type if CPU model is not versioned:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,+arch-capabilities')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                        'pc-i440fx-4.0 + Cascadelake-Server,+arch-capabilities should have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,-arch-capabilities')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server,-arch-capabilities should not have arch-capabilities')

        # versioned CPU model overrides machine-type:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server-v1,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-v1 should not have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server-v2,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-v1 should have arch-capabilities')

        # command line must override machine-type and versioned CPU model:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,+arch-capabilities')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.0 + Cascadelake-Server-v1,+arch-capabilities should have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server-v2,x-force-features=on,check=off,enforce=off,-arch-capabilities')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-v2,-arch-capabilities should not have arch-capabilities')
