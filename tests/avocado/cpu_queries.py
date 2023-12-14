# Sanity check of query-cpu-* results
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest

class QueryCPUModelExpansion(QemuSystemTest):
    """
    Run query-cpu-model-expansion for each CPU model, and validate results
    """

    def test(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:none
        """
        self.vm.add_args('-S')
        self.vm.launch()

        cpus = self.vm.cmd('query-cpu-definitions')
        for c in cpus:
            self.log.info("Checking CPU: %s", c)
            self.assertNotIn('', c['unavailable-features'], c['name'])

        for c in cpus:
            model = {'name': c['name']}
            e = self.vm.cmd('query-cpu-model-expansion', model=model,
                            type='full')
            self.assertEqual(e['model']['name'], c['name'])
