# Simple functional tests for VNC functionality
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import socket
from typing import List

from avocado_qemu import QemuSystemTest


VNC_ADDR = '127.0.0.1'
VNC_PORT_START = 32768
VNC_PORT_END = VNC_PORT_START + 1024


def check_bind(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind((VNC_ADDR, port))
        except OSError:
            return False

    return True


def check_connect(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.connect((VNC_ADDR, port))
        except ConnectionRefusedError:
            return False

    return True


def find_free_ports(count: int) -> List[int]:
    result = []
    for port in range(VNC_PORT_START, VNC_PORT_END):
        if check_bind(port):
            result.append(port)
            if len(result) >= count:
                break
    assert len(result) == count
    return result


class Vnc(QemuSystemTest):
    """
    :avocado: tags=vnc,quick
    :avocado: tags=machine:none
    """
    def test_no_vnc(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.launch()
        self.assertFalse(self.vm.qmp('query-vnc')['return']['enabled'])

    def test_no_vnc_change_password(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.launch()
        self.assertFalse(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change-vnc-password',
                                            password='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

    def test_change_password_requires_a_password(self):
        self.vm.add_args('-nodefaults', '-S', '-vnc', ':0')
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change-vnc-password',
                                            password='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

    def test_change_password(self):
        self.vm.add_args('-nodefaults', '-S', '-vnc', ':0,password=on')
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        self.vm.cmd('change-vnc-password',
                    password='new_password')

    def test_change_listen(self):
        a, b, c = find_free_ports(3)
        self.assertFalse(check_connect(a))
        self.assertFalse(check_connect(b))
        self.assertFalse(check_connect(c))

        self.vm.add_args('-nodefaults', '-S', '-vnc', f'{VNC_ADDR}:{a - 5900}')
        self.vm.launch()
        self.assertEqual(self.vm.qmp('query-vnc')['return']['service'], str(a))
        self.assertTrue(check_connect(a))
        self.assertFalse(check_connect(b))
        self.assertFalse(check_connect(c))

        self.vm.cmd('display-update', type='vnc',
                    addresses=[{'type': 'inet', 'host': VNC_ADDR,
                                'port': str(b)},
                               {'type': 'inet', 'host': VNC_ADDR,
                                'port': str(c)}])
        self.assertEqual(self.vm.qmp('query-vnc')['return']['service'], str(b))
        self.assertFalse(check_connect(a))
        self.assertTrue(check_connect(b))
        self.assertTrue(check_connect(c))
