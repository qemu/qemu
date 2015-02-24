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

import errno
import socket

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

    def accept(self):
        """
        Await connection from QEMU.

        @raise socket.error on socket connection errors
        """
        self._sock, _ = self._sock.accept()

    def cmd(self, qtest_cmd):
        """
        Send a qtest command on the wire.

        @param qtest_cmd: qtest command text to be sent
        """
        self._sock.sendall(qtest_cmd + "\n")

    def close(self):
        self._sock.close()

    def settimeout(self, timeout):
        self._sock.settimeout(timeout)
