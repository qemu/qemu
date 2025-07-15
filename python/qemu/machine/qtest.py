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
    Tuple,
)

from qemu.qmp import SocketAddrT

from .machine import QEMUMachine


class QEMUQtestProtocol:
    """
    QEMUQtestProtocol implements a connection to a qtest socket.

    :param address: QEMU address, can be either a unix socket path (string)
                    or a tuple in the form ( address, port ) for a TCP
                    connection
    :param sock: An existing socket can be provided as an alternative to
                 an address. One of address or sock must be provided.
    :param server: server mode, listens on the socket. Only meaningful
                   in conjunction with an address and not an existing
                   socket.

    :raise socket.error: on socket connection errors

    .. note::
       No connection is established by __init__(), this is done
       by the connect() or accept() methods.
    """
    def __init__(self,
                 address: Optional[SocketAddrT] = None,
                 sock: Optional[socket.socket] = None,
                 server: bool = False):
        if address is None and sock is None:
            raise ValueError("Either 'address' or 'sock' must be specified")
        if address is not None and sock is not None:
            raise ValueError(
                "Either 'address' or 'sock' must be specified, but not both")
        if sock is not None and server:
            raise ValueError("server=True is meaningless when passing socket")

        self._address = address
        self._sock = sock or self._get_sock()
        self._sockfile: Optional[TextIO] = None

        if server:
            assert self._address is not None
            self._sock.bind(self._address)
            self._sock.listen(1)

    def _get_sock(self) -> socket.socket:
        assert self._address is not None
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
        if self._address is not None:
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
                 qmp_timer: Optional[float] = None):
        # pylint: disable=too-many-arguments

        if name is None:
            name = "qemu-%d" % os.getpid()
        super().__init__(binary, args, wrapper=wrapper, name=name,
                         base_temp_dir=base_temp_dir,
                         qmp_timer=qmp_timer)
        self._qtest: Optional[QEMUQtestProtocol] = None
        self._qtest_sock_pair: Optional[
            Tuple[socket.socket, socket.socket]] = None

    @property
    def _base_args(self) -> List[str]:
        args = super()._base_args
        assert self._qtest_sock_pair is not None
        fd = self._qtest_sock_pair[0].fileno()
        args.extend([
            '-chardev', f"socket,id=qtest,fd={fd}",
            '-qtest', 'chardev:qtest',
            '-accel', 'qtest'
        ])
        return args

    def _pre_launch(self) -> None:
        self._qtest_sock_pair = socket.socketpair()
        os.set_inheritable(self._qtest_sock_pair[0].fileno(), True)
        super()._pre_launch()
        self._qtest = QEMUQtestProtocol(sock=self._qtest_sock_pair[1])

    def _post_launch(self) -> None:
        assert self._qtest is not None
        super()._post_launch()
        if self._qtest_sock_pair:
            self._qtest_sock_pair[0].close()
        self._qtest.connect()

    def _post_shutdown(self) -> None:
        if self._qtest_sock_pair:
            self._qtest_sock_pair[0].close()
            self._qtest_sock_pair[1].close()
            self._qtest_sock_pair = None
        if self._qtest is not None:
            self._qtest.close()
        super()._post_shutdown()

    def qtest(self, cmd: str) -> str:
        """
        Send a qtest command to the guest.

        :param cmd: qtest command to send
        :return: qtest server response
        """
        if self._qtest is None:
            raise RuntimeError("qtest socket not available")
        return self._qtest.cmd(cmd)
