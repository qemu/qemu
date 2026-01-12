#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ppc migration test

from qemu_test.ports import Ports
from migration import MigrationTest


class PpcMigrationTest(MigrationTest):

    def test_migration_with_tcp_localhost(self):
        self.set_machine('mac99')
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.set_machine('mac99')
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.set_machine('mac99')
        self.migration_with_exec()

    def do_migrate_ppc64_linux(self, source_vm, dest_vm):
        with Ports() as ports:
            port = ports.find_free_port()
            if port is None:
                self.skipTest('Failed to find a free port')
            uri = 'tcp:localhost:%u' % port

            dest_vm.qmp('migrate-incoming', uri=uri)
            source_vm.qmp('migrate', uri=uri)
            self.assert_migration(source_vm, dest_vm)


if __name__ == '__main__':
    MigrationTest.main()
