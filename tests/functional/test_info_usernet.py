#!/usr/bin/env python3
#
# Test for the hmp command "info usernet"
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest
from qemu_test.utils import get_usernet_hostfwd_port


class InfoUsernet(QemuSystemTest):

    def test_hostfwd(self):
        self.require_netdev('user')
        self.set_machine('none')
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22')
        self.vm.launch()

        port = get_usernet_hostfwd_port(self.vm)
        self.assertIsNotNone(port,
                             ('"info usernet" output content does not seem to '
                              'contain the redirected port'))
        self.assertGreater(port, 0,
                           ('Found a redirected port that is not greater than'
                            ' zero'))

if __name__ == '__main__':
    QemuSystemTest.main()
