"""
QEMU Console Socket Module:

This python module implements a ConsoleSocket object,
which can drain a socket and optionally dump the bytes to file.
"""
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#

from collections import deque
import socket
import threading
import time
from typing import Deque, Optional


class ConsoleSocket(socket.socket):
    """
    ConsoleSocket represents a socket attached to a char device.

    Optionally (if drain==True), drains the socket and places the bytes
    into an in memory buffer for later processing.

    Optionally a file path can be passed in and we will also
    dump the characters to this file for debugging purposes.
    """
    def __init__(self, address: str, file: Optional[str] = None,
                 drain: bool = False):
        self._recv_timeout_sec = 300.0
        self._sleep_time = 0.5
        self._buffer: Deque[int] = deque()
        socket.socket.__init__(self, socket.AF_UNIX, socket.SOCK_STREAM)
        self.connect(address)
        self._logfile = None
        if file:
            # pylint: disable=consider-using-with
            self._logfile = open(file, "bw")
        self._open = True
        self._drain_thread = None
        if drain:
            self._drain_thread = self._thread_start()

    def __repr__(self) -> str:
        tmp = super().__repr__()
        tmp = tmp.rstrip(">")
        tmp = "%s,  logfile=%s, drain_thread=%s>" % (tmp, self._logfile,
                                                     self._drain_thread)
        return tmp

    def _drain_fn(self) -> None:
        """Drains the socket and runs while the socket is open."""
        while self._open:
            try:
                self._drain_socket()
            except socket.timeout:
                # The socket is expected to timeout since we set a
                # short timeout to allow the thread to exit when
                # self._open is set to False.
                time.sleep(self._sleep_time)

    def _thread_start(self) -> threading.Thread:
        """Kick off a thread to drain the socket."""
        # Configure socket to not block and timeout.
        # This allows our drain thread to not block
        # on recieve and exit smoothly.
        socket.socket.setblocking(self, False)
        socket.socket.settimeout(self, 1)
        drain_thread = threading.Thread(target=self._drain_fn)
        drain_thread.daemon = True
        drain_thread.start()
        return drain_thread

    def close(self) -> None:
        """Close the base object and wait for the thread to terminate"""
        if self._open:
            self._open = False
            if self._drain_thread is not None:
                thread, self._drain_thread = self._drain_thread, None
                thread.join()
            socket.socket.close(self)
            if self._logfile:
                self._logfile.close()
                self._logfile = None

    def _drain_socket(self) -> None:
        """process arriving characters into in memory _buffer"""
        data = socket.socket.recv(self, 1)
        if self._logfile:
            self._logfile.write(data)
            self._logfile.flush()
        self._buffer.extend(data)

    def recv(self, bufsize: int = 1, flags: int = 0) -> bytes:
        """Return chars from in memory buffer.
           Maintains the same API as socket.socket.recv.
        """
        if self._drain_thread is None:
            # Not buffering the socket, pass thru to socket.
            return socket.socket.recv(self, bufsize, flags)
        assert not flags, "Cannot pass flags to recv() in drained mode"
        start_time = time.time()
        while len(self._buffer) < bufsize:
            time.sleep(self._sleep_time)
            elapsed_sec = time.time() - start_time
            if elapsed_sec > self._recv_timeout_sec:
                raise socket.timeout
        return bytes((self._buffer.popleft() for i in range(bufsize)))

    def setblocking(self, value: bool) -> None:
        """When not draining we pass thru to the socket,
           since when draining we control socket blocking.
        """
        if self._drain_thread is None:
            socket.socket.setblocking(self, value)

    def settimeout(self, value: Optional[float]) -> None:
        """When not draining we pass thru to the socket,
           since when draining we control the timeout.
        """
        if value is not None:
            self._recv_timeout_sec = value
        if self._drain_thread is None:
            socket.socket.settimeout(self, value)
