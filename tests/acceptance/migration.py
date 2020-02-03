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


from avocado_qemu import Test

from avocado.utils import network
from avocado.utils import wait


class Migration(Test):

    timeout = 10

    @staticmethod
    def migration_finished(vm):
        return vm.command('query-migrate')['status'] in ('completed', 'failed')

    def assert_migration(self, src_vm, dst_vm):
        wait.wait_for(self.migration_finished,
                      timeout=self.timeout,
                      step=0.1,
                      args=(src_vm,))
        self.assertEqual(src_vm.command('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.command('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.command('query-status')['status'], 'running')
        self.assertEqual(src_vm.command('query-status')['status'],'postmigrate')

    def do_migrate(self, dest_uri, src_uri=None):
        source_vm = self.get_vm()
        dest_vm = self.get_vm('-incoming', dest_uri)
        dest_vm.launch()
        if src_uri is None:
            src_uri = dest_uri
        source_vm.launch()
        source_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(source_vm, dest_vm)

    def _get_free_port(self):
        port = network.find_free_port()
        if port is None:
            self.cancel('Failed to find a free port')
        return port


    def test_migration_with_tcp_localhost(self):
        dest_uri = 'tcp:localhost:%u' % self._get_free_port()
        self.do_migrate(dest_uri)
