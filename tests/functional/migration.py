# SPDX-License-Identifier: GPL-2.0-or-later
#
# Migration test base class
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#  Caio Carrara <ccarrara@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time

from qemu_test import QemuSystemTest, which
from qemu_test.ports import Ports


class MigrationTest(QemuSystemTest):

    timeout = 10

    @staticmethod
    def migration_finished(vm):
        return vm.cmd('query-migrate')['status'] in ('completed', 'failed')

    def assert_migration(self, src_vm, dst_vm):

        end = time.monotonic() + self.timeout
        while time.monotonic() < end and not self.migration_finished(src_vm):
            time.sleep(0.1)

        end = time.monotonic() + self.timeout
        while time.monotonic() < end and not self.migration_finished(dst_vm):
            time.sleep(0.1)

        self.assertEqual(src_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-status')['status'], 'running')
        self.assertEqual(src_vm.cmd('query-status')['status'],'postmigrate')

    def migrate_vms(self, dst_uri, src_uri, dst_vm, src_vm):
        dst_vm.qmp('migrate-incoming', uri=dst_uri)
        src_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(src_vm, dst_vm)

    def migrate(self, dst_uri, src_uri=None):
        dst_vm = self.get_vm('-incoming', 'defer', name="dst-qemu")
        dst_vm.add_args('-nodefaults')
        dst_vm.launch()

        src_vm = self.get_vm(name="src-qemu")
        src_vm.add_args('-nodefaults')
        src_vm.launch()

        if src_uri is None:
            src_uri = dst_uri

        self.migrate_vms(dst_uri, src_uri, dst_vm, src_vm)

    def _get_free_port(self, ports):
        port = ports.find_free_port()
        if port is None:
            self.skipTest('Failed to find a free port')
        return port

    def migration_with_tcp_localhost(self):
        with Ports() as ports:
            dst_uri = 'tcp:localhost:%u' % self._get_free_port(ports)
            self.migrate(dst_uri)

    def migration_with_unix(self):
        dst_uri = 'unix:%s/migration.sock' % self.socket_dir().name
        self.migrate(dst_uri)

    def migration_with_exec(self):
        if not which('ncat'):
            self.skipTest('ncat is not available')
        with Ports() as ports:
            free_port = self._get_free_port(ports)
            dst_uri = 'exec:ncat -l localhost %u' % free_port
            src_uri = 'exec:ncat localhost %u' % free_port
            self.migrate(dst_uri, src_uri)
