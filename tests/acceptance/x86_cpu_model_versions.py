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
