#!/usr/bin/env python3
#
# This python module implements a ConsoleSocket object which is
# designed always drain the socket itself, and place
# the bytes into a in memory buffer for later processing.
#
# Optionally a file path can be passed in and we will also
# dump the characters to this file for debug.
#
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#
import asyncore
import socket
import threading
import io
import os
import sys
from collections import deque
import time
import traceback

class ConsoleSocket(asyncore.dispatcher):

    def __init__(self, address, file=None):
        self._recv_timeout_sec = 300
        self._buffer = deque()
        self._asyncore_thread = None
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.connect(address)
        self._logfile = None
        if file:
            self._logfile = open(file, "w")
        asyncore.dispatcher.__init__(self, sock=self._sock)
        self._open = True
        self._thread_start()

    def _thread_start(self):
        """Kick off a thread to wait on the asyncore.loop"""
        if self._asyncore_thread is not None:
            return
        self._asyncore_thread = threading.Thread(target=asyncore.loop,
                                                 kwargs={'timeout':1})
        self._asyncore_thread.daemon = True
        self._asyncore_thread.start()

    def handle_close(self):
        """redirect close to base class"""
        # Call the base class close, but not self.close() since
        # handle_close() occurs in the context of the thread which
        # self.close() attempts to join.
        asyncore.dispatcher.close(self)

    def close(self):
        """Close the base object and wait for the thread to terminate"""
        if self._open:
            self._open = False
            asyncore.dispatcher.close(self)
            if self._asyncore_thread is not None:
                thread, self._asyncore_thread = self._asyncore_thread, None
                thread.join()
            if self._logfile:
                self._logfile.close()
                self._logfile = None

    def handle_read(self):
        """process arriving characters into in memory _buffer"""
        try:
            data = asyncore.dispatcher.recv(self, 1)
            # latin1 is needed since there are some chars
            # we are receiving that cannot be encoded to utf-8
            # such as 0xe2, 0x80, 0xA6.
            string = data.decode("latin1")
        except:
            print("Exception seen.")
            traceback.print_exc()
            return
        if self._logfile:
            self._logfile.write("{}".format(string))
            self._logfile.flush()
        for c in string:
            self._buffer.extend(c)

    def recv(self, n=1, sleep_delay_s=0.1):
        """Return chars from in memory buffer"""
        start_time = time.time()
        while len(self._buffer) < n:
            time.sleep(sleep_delay_s)
            elapsed_sec = time.time() - start_time
            if elapsed_sec > self._recv_timeout_sec:
                raise socket.timeout
        chars = ''.join([self._buffer.popleft() for i in range(n)])
        # We choose to use latin1 to remain consistent with
        # handle_read() and give back the same data as the user would
        # receive if they were reading directly from the
        # socket w/o our intervention.
        return chars.encode("latin1")

    def set_blocking(self):
        """Maintain compatibility with socket API"""
        pass

    def settimeout(self, seconds):
        """Set current timeout on recv"""
        self._recv_timeout_sec = seconds
