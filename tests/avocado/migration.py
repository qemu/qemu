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
import os

from avocado_qemu import QemuSystemTest
from avocado import skipUnless

from avocado.utils.network import ports
from avocado.utils import wait
from avocado.utils.path import find_command


class MigrationTest(QemuSystemTest):
    """
    :avocado: tags=migration
    """

    timeout = 10

    @staticmethod
    def migration_finished(vm):
        return vm.cmd('query-migrate')['status'] in ('completed', 'failed')

    def assert_migration(self, src_vm, dst_vm):
        wait.wait_for(self.migration_finished,
                      timeout=self.timeout,
                      step=0.1,
                      args=(src_vm,))
        wait.wait_for(self.migration_finished,
                      timeout=self.timeout,
                      step=0.1,
                      args=(dst_vm,))
        self.assertEqual(src_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-status')['status'], 'running')
        self.assertEqual(src_vm.cmd('query-status')['status'],'postmigrate')

    def do_migrate(self, dest_uri, src_uri=None):
        dest_vm = self.get_vm('-incoming', dest_uri)
        dest_vm.add_args('-nodefaults')
        dest_vm.launch()
        if src_uri is None:
            src_uri = dest_uri
        source_vm = self.get_vm()
        source_vm.add_args('-nodefaults')
        source_vm.launch()
        source_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(source_vm, dest_vm)

    def _get_free_port(self):
        port = ports.find_free_port()
        if port is None:
            self.cancel('Failed to find a free port')
        return port

    def migration_with_tcp_localhost(self):
        dest_uri = 'tcp:localhost:%u' % self._get_free_port()
        self.do_migrate(dest_uri)

    def migration_with_unix(self):
        with tempfile.TemporaryDirectory(prefix='socket_') as socket_path:
            dest_uri = 'unix:%s/qemu-test.sock' % socket_path
            self.do_migrate(dest_uri)

    @skipUnless(find_command('nc', default=False), "'nc' command not found")
    def migration_with_exec(self):
        """The test works for both netcat-traditional and netcat-openbsd packages."""
        free_port = self._get_free_port()
        dest_uri = 'exec:nc -l localhost %u' % free_port
        src_uri = 'exec:nc localhost %u' % free_port
        self.do_migrate(dest_uri, src_uri)


@skipUnless('aarch64' in os.uname()[4], "host != target")
class Aarch64(MigrationTest):
    """
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:virt
    :avocado: tags=cpu:max
    """

    def test_migration_with_tcp_localhost(self):
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.migration_with_exec()


@skipUnless('x86_64' in os.uname()[4], "host != target")
class X86_64(MigrationTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=machine:pc
    :avocado: tags=cpu:qemu64
    """

    def test_migration_with_tcp_localhost(self):
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.migration_with_exec()


@skipUnless('ppc64le' in os.uname()[4], "host != target")
class PPC64(MigrationTest):
    """
    :avocado: tags=arch:ppc64
    :avocado: tags=machine:pseries
    :avocado: tags=cpu:power9_v2.0
    """

    def test_migration_with_tcp_localhost(self):
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.migration_with_exec()
