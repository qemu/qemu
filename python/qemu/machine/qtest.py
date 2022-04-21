"""
QEMU qtest library

qtest offers the QEMUQtestProtocol and QEMUQTestMachine classes, which
offer a connection to QEMU's qtest protocol socket, and a qtest-enabled
subclass of QEMUMachine, respectively.
"""

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

import os
import socket
from typing import (
    List,
    Optional,
    Sequence,
    TextIO,
)

from qemu.qmp import SocketAddrT

from .machine import QEMUMachine


class QEMUQtestProtocol:
    """
    QEMUQtestProtocol implements a connection to a qtest socket.

    :param address: QEMU address, can be either a unix socket path (string)
                    or a tuple in the form ( address, port ) for a TCP
                    connection
    :param server: server mode, listens on the socket (bool)
    :raise socket.error: on socket connection errors

    .. note::
       No conection is estabalished by __init__(), this is done
       by the connect() or accept() methods.
    """
    def __init__(self, address: SocketAddrT,
                 server: bool = False):
        self._address = address
        self._sock = self._get_sock()
        self._sockfile: Optional[TextIO] = None
        if server:
            self._sock.bind(self._address)
            self._sock.listen(1)

    def _get_sock(self) -> socket.socket:
        if isinstance(self._address, tuple):
            family = socket.AF_INET
        else:
            family = socket.AF_UNIX
        return socket.socket(family, socket.SOCK_STREAM)

    def connect(self) -> None:
        """
        Connect to the qtest socket.

        @raise socket.error on socket connection errors
        """
        self._sock.connect(self._address)
        self._sockfile = self._sock.makefile(mode='r')

    def accept(self) -> None:
        """
        Await connection from QEMU.

        @raise socket.error on socket connection errors
        """
        self._sock, _ = self._sock.accept()
        self._sockfile = self._sock.makefile(mode='r')

    def cmd(self, qtest_cmd: str) -> str:
        """
        Send a qtest command on the wire.

        @param qtest_cmd: qtest command text to be sent
        """
        assert self._sockfile is not None
        self._sock.sendall((qtest_cmd + "\n").encode('utf-8'))
        resp = self._sockfile.readline()
        return resp

    def close(self) -> None:
        """
        Close this socket.
        """
        self._sock.close()
        if self._sockfile:
            self._sockfile.close()
            self._sockfile = None

    def settimeout(self, timeout: Optional[float]) -> None:
        """Set a timeout, in seconds."""
        self._sock.settimeout(timeout)


class QEMUQtestMachine(QEMUMachine):
    """
    A QEMU VM, with a qtest socket available.
    """

    def __init__(self,
                 binary: str,
                 args: Sequence[str] = (),
                 wrapper: Sequence[str] = (),
                 name: Optional[str] = None,
                 base_temp_dir: str = "/var/tmp",
                 sock_dir: Optional[str] = None,
                 qmp_timer: Optional[float] = None):
        # pylint: disable=too-many-arguments

        if name is None:
            name = "qemu-%d" % os.getpid()
        if sock_dir is None:
            sock_dir = base_temp_dir
        super().__init__(binary, args, wrapper=wrapper, name=name,
                         base_temp_dir=base_temp_dir,
                         sock_dir=sock_dir, qmp_timer=qmp_timer)
        self._qtest: Optional[QEMUQtestProtocol] = None
        self._qtest_path = os.path.join(sock_dir, name + "-qtest.sock")

    @property
    def _base_args(self) -> List[str]:
        args = super()._base_args
        args.extend([
            '-qtest', f"unix:path={self._qtest_path}",
            '-accel', 'qtest'
        ])
        return args

    def _pre_launch(self) -> None:
        super()._pre_launch()
        self._qtest = QEMUQtestProtocol(self._qtest_path, server=True)

    def _post_launch(self) -> None:
        assert self._qtest is not None
        super()._post_launch()
        self._qtest.accept()

    def _post_shutdown(self) -> None:
        super()._post_shutdown()
        self._remove_if_exists(self._qtest_path)

    def qtest(self, cmd: str) -> str:
        """
        Send a qtest command to the guest.

        :param cmd: qtest command to send
        :return: qtest server response
        """
        if self._qtest is None:
            raise RuntimeError("qtest socket not available")
        return self._qtest.cmd(cmd)
