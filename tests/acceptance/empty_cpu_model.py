# Check for crash when using empty -cpu option
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import subprocess
from avocado_qemu import Test

class EmptyCPUModel(Test):
    def test(self):
        cmd = [self.qemu_bin, '-S', '-display', 'none', '-machine', 'none', '-cpu', '']
        r = subprocess.run(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        self.assertEquals(r.returncode, 1, "QEMU exit code should be 1")
        self.assertEquals(r.stdout, b'', "QEMU stdout should be empty")
        self.assertNotEquals(r.stderr, b'', "QEMU stderr shouldn't be empty")
