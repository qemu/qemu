# Check for crash when using empty -cpu option
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
from avocado_qemu import Test

class EmptyCPUModel(Test):
    def test(self):
        self.vm.add_args('-S', '-display', 'none', '-machine', 'none', '-cpu', '')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEquals(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'-cpu option cannot be empty')
