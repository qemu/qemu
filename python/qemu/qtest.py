# QEMU qtest library
#
# Copyright (C) 2015 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Based on qmp.py.
#

import socket
import os

from .machine import QEMUMachine


class QEMUQtestProtocol(object):
    def __init__(self, address, server=False):
        """
        Create a QEMUQtestProtocol object.

        @param address: QEMU address, can be either a unix socket path (string)
                        or a tuple in the form ( address, port ) for a TCP
                        connection
        @param server: server mode, listens on the socket (bool)
        @raise socket.error on socket connection errors
        @note No connection is established, this is done by the connect() or
              accept() methods
        """
        self._address = address
        self._sock = self._get_sock()
        self._sockfile = None
        if server:
            self._sock.bind(self._address)
            self._sock.listen(1)

    def _get_sock(self):
        if isinstance(self._address, tuple):
            family = socket.AF_INET
        else:
            family = socket.AF_UNIX
        return socket.socket(family, socket.SOCK_STREAM)

    def connect(self):
        """
        Connect to the qtest socket.

        @raise socket.error on socket connection errors
        """
        self._sock.connect(self._address)
        self._sockfile = self._sock.makefile()

    def accept(self):
        """
        Await connection from QEMU.

        @raise socket.error on socket connection errors
        """
        self._sock, _ = self._sock.accept()
        self._sockfile = self._sock.makefile()

    def cmd(self, qtest_cmd):
        """
        Send a qtest command on the wire.

        @param qtest_cmd: qtest command text to be sent
        """
        self._sock.sendall((qtest_cmd + "\n").encode('utf-8'))
        resp = self._sockfile.readline()
        return resp

    def close(self):
        self._sock.close()
        self._sockfile.close()

    def settimeout(self, timeout):
        self._sock.settimeout(timeout)


class QEMUQtestMachine(QEMUMachine):
    '''A QEMU VM'''

    def __init__(self, binary, args=None, name=None, test_dir="/var/tmp",
                 socket_scm_helper=None, sock_dir=None):
        if name is None:
            name = "qemu-%d" % os.getpid()
        if sock_dir is None:
            sock_dir = test_dir
        super(QEMUQtestMachine,
              self).__init__(binary, args, name=name, test_dir=test_dir,
                             socket_scm_helper=socket_scm_helper,
                             sock_dir=sock_dir)
        self._qtest = None
        self._qtest_path = os.path.join(sock_dir, name + "-qtest.sock")

    def _base_args(self):
        args = super(QEMUQtestMachine, self)._base_args()
        args.extend(['-qtest', 'unix:path=' + self._qtest_path,
                     '-accel', 'qtest'])
        return args

    def _pre_launch(self):
        super(QEMUQtestMachine, self)._pre_launch()
        self._qtest = QEMUQtestProtocol(self._qtest_path, server=True)

    def _post_launch(self):
        super(QEMUQtestMachine, self)._post_launch()
        self._qtest.accept()

    def _post_shutdown(self):
        super(QEMUQtestMachine, self)._post_shutdown()
        self._remove_if_exists(self._qtest_path)

    def qtest(self, cmd):
        '''Send a qtest command to guest'''
        return self._qtest.cmd(cmd)
