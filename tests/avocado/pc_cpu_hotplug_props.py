#
# Ensure CPU die-id can be omitted on -device
#
#  Copyright (c) 2019 Red Hat Inc
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

from avocado_qemu import QemuSystemTest

class OmittedCPUProps(QemuSystemTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=cpu:qemu64
    """
    def test_no_die_id(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.add_args('-smp', '1,sockets=2,cores=2,threads=2,maxcpus=8')
        self.vm.add_args('-device', 'qemu64-x86_64-cpu,socket-id=1,core-id=0,thread-id=0')
        self.vm.launch()
        self.assertEquals(len(self.vm.command('query-cpus-fast')), 2)
