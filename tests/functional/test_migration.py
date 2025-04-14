#!/usr/bin/env python3
#
# Migration test
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#  Caio Carrara <ccarrara@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import tempfile
import time

from qemu_test import QemuSystemTest, skipIfMissingCommands
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

    def select_machine(self):
        target_machine = {
            'aarch64': 'quanta-gsj',
            'alpha': 'clipper',
            'arm': 'npcm750-evb',
            'i386': 'isapc',
            'ppc': 'sam460ex',
            'ppc64': 'mac99',
            'riscv32': 'spike',
            'riscv64': 'virt',
            'sparc': 'SS-4',
            'sparc64': 'sun4u',
            'x86_64': 'microvm',
        }
        self.set_machine(target_machine[self.arch])

    def do_migrate(self, dest_uri, src_uri=None):
        self.select_machine()
        dest_vm = self.get_vm('-incoming', dest_uri, name="dest-qemu")
        dest_vm.add_args('-nodefaults')
        dest_vm.launch()
        if src_uri is None:
            src_uri = dest_uri
        source_vm = self.get_vm(name="source-qemu")
        source_vm.add_args('-nodefaults')
        source_vm.launch()
        source_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(source_vm, dest_vm)

    def _get_free_port(self, ports):
        port = ports.find_free_port()
        if port is None:
            self.skipTest('Failed to find a free port')
        return port

    def test_migration_with_tcp_localhost(self):
        with Ports() as ports:
            dest_uri = 'tcp:localhost:%u' % self._get_free_port(ports)
            self.do_migrate(dest_uri)

    def test_migration_with_unix(self):
        with tempfile.TemporaryDirectory(prefix='socket_') as socket_path:
            dest_uri = 'unix:%s/qemu-test.sock' % socket_path
            self.do_migrate(dest_uri)

    @skipIfMissingCommands('ncat')
    def test_migration_with_exec(self):
        with Ports() as ports:
            free_port = self._get_free_port(ports)
            dest_uri = 'exec:ncat -l localhost %u' % free_port
            src_uri = 'exec:ncat localhost %u' % free_port
            self.do_migrate(dest_uri, src_uri)

if __name__ == '__main__':
    QemuSystemTest.main()
