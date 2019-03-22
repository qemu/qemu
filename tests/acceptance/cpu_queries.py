# Sanity check of query-cpu-* results
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging

from avocado_qemu import Test

class QueryCPUModelExpansion(Test):
    """
    Run query-cpu-model-expansion for each CPU model, and validate results
    """

    def test(self):
        self.vm.set_machine('none')
        self.vm.add_args('-S')
        self.vm.launch()

        cpus = self.vm.command('query-cpu-definitions')
        for c in cpus:
            print(repr(c))
            self.assertNotIn('', c['unavailable-features'], c['name'])

        for c in cpus:
            model = {'name': c['name']}
            e = self.vm.command('query-cpu-model-expansion', model=model, type='full')
            self.assertEquals(e['model']['name'], c['name'])
