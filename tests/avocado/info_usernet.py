# Test for the hmp command "info usernet"
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest

from qemu.utils import get_info_usernet_hostfwd_port


class InfoUsernet(QemuSystemTest):
    """
    :avocado: tags=machine:none
    """

    def test_hostfwd(self):
        self.require_netdev('user')
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22')
        self.vm.launch()
        res = self.vm.command('human-monitor-command',
                              command_line='info usernet')
        port = get_info_usernet_hostfwd_port(res)
        self.assertIsNotNone(port,
                             ('"info usernet" output content does not seem to '
                              'contain the redirected port'))
        self.assertGreater(port, 0,
                           ('Found a redirected port that is not greater than'
                            ' zero'))
